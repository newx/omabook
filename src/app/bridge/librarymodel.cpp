#include "librarymodel.h"
#include "importworker.h"

#include "app/assets.h"
#include "core/db/database.h"
#include "core/import/covers.h"
#include "core/import/pdf.h"
#include "core/repo/bookrepository.h"

#include <QModelIndex>
#include <QSqlDatabase>
#include <QStringList>
#include <QThread>

namespace {

// Parse the string QML sends into a filter. Unknown input falls back to All
// rather than showing nothing, which would look like data loss.
LibraryFilter parseFilter(const QString &text) {
    if (text == QStringLiteral("favorites"))
        return LibraryFilter::favorites();
    if (text == QStringLiteral("reading"))
        return LibraryFilter::reading();
    if (text == QStringLiteral("queue"))
        return LibraryFilter::queue();
    if (text == QStringLiteral("completed"))
        return LibraryFilter::completed();

    if (text.startsWith(QStringLiteral("category:"))) {
        bool ok = false;
        const qint64 id = text.mid(9).toLongLong(&ok);
        if (ok)
            return LibraryFilter::category(id);
    }
    if (text.startsWith(QStringLiteral("tag:"))) {
        bool ok = false;
        const qint64 id = text.mid(4).toLongLong(&ok);
        if (ok)
            return LibraryFilter::tag(id);
    }
    return LibraryFilter::all();
}

} // namespace

LibraryModel::LibraryModel(QObject *parent) : QAbstractListModel(parent) { }

LibraryModel::~LibraryModel() {
    // Only reached if the app closes mid-import: the worker thread outlives
    // this object otherwise (it deletes itself on completion). Quitting and
    // waiting here is what keeps ~QThread from being destroyed while still
    // running.
    if (m_importThread && m_importThread->isRunning()) {
        m_importThread->quit();
        m_importThread->wait();
    }
}

int LibraryModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid())
        return 0;
    return m_books.size();
}

QVariant LibraryModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_books.size())
        return QVariant();

    const Book &book = m_books.at(index.row());
    switch (role) {
    case TitleRole:
        return book.title;
    case AuthorRole:
        return book.authorLine();
    case ProgressRole:
        return book.progress;
    case StatusRole:
        return toString(book.status);
    case FormatRole:
        return toString(book.format);
    case BookIdRole:
        return book.id;
    case CoverUrlRole:
        // An empty string means "no cover"; QML shows a placeholder.
        return book.coverPath.isEmpty() ? QString() : toFileUrl(book.coverPath);
    case IsFavoriteRole:
        return book.isFavorite;
    case IsQueuedRole:
        return m_queued.contains(book.id);
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> LibraryModel::roleNames() const {
    return {
        { TitleRole, QByteArrayLiteral("title") },
        { AuthorRole, QByteArrayLiteral("author") },
        { ProgressRole, QByteArrayLiteral("progress") },
        { StatusRole, QByteArrayLiteral("status") },
        { FormatRole, QByteArrayLiteral("format") },
        { BookIdRole, QByteArrayLiteral("bookId") },
        { CoverUrlRole, QByteArrayLiteral("coverUrl") },
        { IsFavoriteRole, QByteArrayLiteral("isFavorite") },
        { IsQueuedRole, QByteArrayLiteral("isQueued") },
    };
}

void LibraryModel::reload() {
    QSqlDatabase &db = Database::forCurrentThread().connection();
    BookRepository repo(db);

    QList<Book> books;
    QString strategy;

    if (!m_ranked.isEmpty()) {
        const Result<QList<Book>> loaded = repo.listIds(m_ranked);
        if (loaded.isErr()) {
            qWarning("could not list ranked books: %s", qUtf8Printable(loaded.error().message));
            setStatusLine(QStringLiteral("could not list books: %1").arg(loaded.error().message));
            return;
        }
        books = loaded.value();
        strategy = QStringLiteral("ranked");
    } else if (!m_search.trimmed().isEmpty()) {
        // A search looks across the whole library; narrowing it by the
        // sidebar selection as well would mostly produce empty results.
        const Result<SearchResult> found = repo.search(m_search, BookSort::Title);
        if (found.isErr()) {
            qWarning("could not search books: %s", qUtf8Printable(found.error().message));
            setStatusLine(QStringLiteral("could not list books: %1").arg(found.error().message));
            return;
        }
        books = found.value().books;
        switch (found.value().strategy) {
        case SearchStrategy::FullText:
            strategy = QStringLiteral("full-text");
            break;
        case SearchStrategy::Substring:
            strategy = QStringLiteral("substring");
            break;
        case SearchStrategy::Empty:
            strategy = QString();
            break;
        }
    } else {
        const Result<QList<Book>> loaded = repo.list(parseFilter(m_filter), BookSort::RecentlyAdded);
        if (loaded.isErr()) {
            qWarning("could not list books: %s", qUtf8Printable(loaded.error().message));
            setStatusLine(QStringLiteral("could not list books: %1").arg(loaded.error().message));
            return;
        }
        books = loaded.value();
        strategy = QString();
    }

    // The queue membership for the books now loaded, in one query -- never
    // a query per row.
    QSet<qint64> queued;
    for (const Book &book : books) {
        const Result<bool> isQueued = repo.isQueued(book.id);
        if (isQueued.isOk() && isQueued.value())
            queued.insert(book.id);
    }

    beginResetModel();
    m_books = books;
    m_queued = queued;
    endResetModel();

    setSearchStrategy(strategy);
    setCount(m_books.size());
    setStatusLine(QString());
}

void LibraryModel::setFilterAndReload(const QString &filter) {
    // Picking a sidebar entry clears a search or a ranked answer, otherwise
    // the click would appear to do nothing.
    setSearch(QString());
    m_ranked.clear();
    setFilter(filter);
    reload();
}

void LibraryModel::setSearchAndReload(const QString &query) {
    m_ranked.clear();
    setSearch(query);
    reload();
}

void LibraryModel::showRankedBooks(const QString &ids) {
    QList<qint64> parsed;
    const QStringList parts = ids.split(QLatin1Char(','));
    for (const QString &part : parts) {
        bool ok = false;
        const qint64 id = part.trimmed().toLongLong(&ok);
        if (ok)
            parsed.append(id);
    }

    setSearch(QString());
    m_ranked = parsed;
    reload();
}

void LibraryModel::toggleFavorite(qint64 bookId) {
    QSqlDatabase &db = Database::forCurrentThread().connection();
    const Result<bool> toggled = BookRepository(db).toggleFavorite(bookId);
    if (toggled.isErr())
        qWarning("could not toggle favourite for book %lld: %s", bookId,
                 qUtf8Printable(toggled.error().message));
    reload();
}

void LibraryModel::toggleQueued(qint64 bookId) {
    QSqlDatabase &db = Database::forCurrentThread().connection();
    const Result<bool> toggled = BookRepository(db).toggleQueued(bookId);
    if (toggled.isErr())
        qWarning("could not toggle the reading queue for book %lld: %s", bookId,
                 qUtf8Printable(toggled.error().message));
    reload();
}

void LibraryModel::deleteBook(qint64 bookId) {
    QSqlDatabase &db = Database::forCurrentThread().connection();
    const Result<void> removed = BookRepository(db).deleteBook(bookId);
    if (removed.isErr())
        qWarning("could not delete book %lld: %s", bookId, qUtf8Printable(removed.error().message));

    // A ranked answer still listing the deleted book would be stale.
    m_ranked.removeAll(bookId);
    reload();
}

void LibraryModel::moveQueued(int from, int to) {
    // Guarded to the raw, unfiltered, unsearched queue on purpose: every
    // other listing is ordered by the database, so a move there would be
    // undone by the next reload.
    const bool showingQueue = parseFilter(m_filter).kind == LibraryFilter::Kind::Queue
        && m_ranked.isEmpty() && m_search.trimmed().isEmpty();
    if (!showingQueue)
        return;

    const int len = m_books.size();
    if (from == to || from < 0 || to < 0 || from >= len || to >= len)
        return;

    // Qt counts the destination in the list as it is *before* the row
    // leaves it, so a move further down lands one past its final index.
    const int destination = to > from ? to + 1 : to;
    const QModelIndex parentIndex;
    if (!beginMoveRows(parentIndex, from, from, parentIndex, destination))
        return;

    m_books.move(from, to);
    endMoveRows();

    QList<qint64> ids;
    ids.reserve(m_books.size());
    for (const Book &book : m_books)
        ids.append(book.id);

    QSqlDatabase &db = Database::forCurrentThread().connection();
    const Result<void> saved = BookRepository(db).reorderQueue(ids);
    if (saved.isErr()) {
        qWarning("could not save the reading queue's order: %s",
                 qUtf8Printable(saved.error().message));
        setStatusLine(QStringLiteral("could not save the reading queue's order"));
    }
}

void LibraryModel::saveProgress(qint64 bookId, const QString &position, double fraction) {
    QSqlDatabase &db = Database::forCurrentThread().connection();
    const Result<void> saved = BookRepository(db).saveProgress(bookId, position, fraction);
    if (saved.isErr())
        qWarning("could not save progress for book %lld: %s", bookId,
                 qUtf8Printable(saved.error().message));
    reload();
}

QString LibraryModel::readingPathFor(qint64 bookId) const {
    QSqlDatabase &db = Database::forCurrentThread().connection();
    const Result<std::optional<Book>> found = BookRepository(db).find(bookId);
    if (found.isErr() || !found.value().has_value())
        return QString();
    return found.value()->readablePath();
}

QString LibraryModel::titleFor(qint64 bookId) const {
    QSqlDatabase &db = Database::forCurrentThread().connection();
    const Result<std::optional<Book>> found = BookRepository(db).find(bookId);
    if (found.isErr() || !found.value().has_value())
        return QString();
    return found.value()->title;
}

QString LibraryModel::textQualityFor(qint64 bookId) const {
    QSqlDatabase &db = Database::forCurrentThread().connection();
    const Result<std::optional<Book>> found = BookRepository(db).find(bookId);
    if (found.isErr() || !found.value().has_value())
        return QString();

    const std::optional<TextQuality> quality = found.value()->textQuality;
    if (!quality.has_value())
        return QString();
    return toString(*quality);
}

QString LibraryModel::pdfPageText(qint64 bookId, int page) const {
    if (page < 1)
        return QString();

    QSqlDatabase &db = Database::forCurrentThread().connection();
    const Result<std::optional<Book>> found = BookRepository(db).find(bookId);
    if (found.isErr() || !found.value().has_value())
        return QString();

    const Book &book = *found.value();
    if (book.format != BookFormat::Pdf)
        return QString();

    return Pdf::extractText(book.readablePath(), page, page).value_or(QString());
}

QString LibraryModel::readerUrlFor(qint64 bookId) const {
    QSqlDatabase &db = Database::forCurrentThread().connection();
    BookRepository repo(db);

    const Result<std::optional<Book>> found = repo.find(bookId);
    if (found.isErr() || !found.value().has_value())
        return QString();

    const Result<std::optional<QString>> position = repo.progressPosition(bookId);
    const QString cfi = (position.isOk() && position.value().has_value()) ? *position.value() : QString();

    return Assets::readerUrl(found.value()->readablePath(), cfi);
}

void LibraryModel::importDirectory(const QString &path) {
    if (m_busy)
        return;

    setBusy(true);
    setStatusLine(QStringLiteral("scanning…"));

    auto *thread = new QThread(this);
    auto *worker = new ImportWorker; // no parent, or moveToThread silently does nothing
    worker->moveToThread(thread);

    connect(thread, &QThread::started, worker, [worker, path]() { worker->run(path); });
    connect(worker, &ImportWorker::progress, this, &LibraryModel::onImportProgress);
    connect(worker, &ImportWorker::finished, this, &LibraryModel::onImportFinished);
    connect(worker, &ImportWorker::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    m_importThread = thread;
    thread->start();
}

void LibraryModel::onImportProgress(const QString &text) {
    setStatusLine(text);
}

void LibraryModel::onImportFinished(const QString &message) {
    // Status and rows before busy, so a Connections.onBusyChanged handler in
    // QML does not read this run's results before they are written
    // (SPEC §5.13; CLAUDE.md "Threading").
    setStatusLine(message);
    reload();
    setBusy(false);
}

void LibraryModel::setCount(int count) {
    if (m_count == count)
        return;

    m_count = count;
    emit countChanged();
}

void LibraryModel::setStatusLine(const QString &statusLine) {
    if (m_statusLine == statusLine)
        return;

    m_statusLine = statusLine;
    emit statusLineChanged();
}

void LibraryModel::setFilter(const QString &filter) {
    if (m_filter == filter)
        return;

    m_filter = filter;
    emit filterChanged();
}

void LibraryModel::setSearch(const QString &search) {
    if (m_search == search)
        return;

    m_search = search;
    emit searchChanged();
}

void LibraryModel::setSearchStrategy(const QString &strategy) {
    if (m_searchStrategy == strategy)
        return;

    m_searchStrategy = strategy;
    emit searchStrategyChanged();
}

void LibraryModel::setBusy(bool busy) {
    if (m_busy == busy)
        return;

    m_busy = busy;
    emit busyChanged();
}
