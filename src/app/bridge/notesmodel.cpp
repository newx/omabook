#include "notesmodel.h"

#include "core/db/database.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <optional>

namespace {

// An empty string means "not set", not "set to empty" -- matches the
// reader's NoteDialog and the highlight toolbar, neither of which can send
// an empty CFI, quote or body on purpose.
std::optional<QString> nonEmpty(const QString &value) {
    return value.trimmed().isEmpty() ? std::nullopt : std::make_optional(value);
}

} // namespace

NotesModel::NotesModel(QObject *parent) : QAbstractListModel(parent) { }

int NotesModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid())
        return 0;
    return m_notes.size();
}

QVariant NotesModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_notes.size())
        return QVariant();

    const Note &note = m_notes.at(index.row());
    switch (role) {
    case NoteIdRole:
        return note.id;
    case BookIdRole:
        return note.bookId;
    case BookTitleRole:
        return note.bookTitle;
    case QuoteRole:
        return note.quote.value_or(QString());
    case BodyRole:
        return note.body.value_or(QString());
    case CfiRole:
        return note.cfi.value_or(QString());
    case IsHighlightRole:
        return note.isHighlightOnly();
    case CreatedAtRole:
        return datePortion(note.createdAt);
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> NotesModel::roleNames() const {
    return {
        { NoteIdRole, QByteArrayLiteral("noteId") },
        { BookIdRole, QByteArrayLiteral("bookId") },
        { BookTitleRole, QByteArrayLiteral("bookTitle") },
        { QuoteRole, QByteArrayLiteral("quote") },
        { BodyRole, QByteArrayLiteral("body") },
        { CfiRole, QByteArrayLiteral("cfi") },
        { IsHighlightRole, QByteArrayLiteral("isHighlight") },
        { CreatedAtRole, QByteArrayLiteral("createdAt") },
    };
}

QString NotesModel::datePortion(const QString &timestamp) {
    const int split = timestamp.indexOf(QLatin1Char('T'));
    return split < 0 ? timestamp : timestamp.left(split);
}

void NotesModel::setCount(int count) {
    if (m_count == count)
        return;

    m_count = count;
    emit countChanged();
}

void NotesModel::setBookId(qint64 bookId) {
    if (m_bookId == bookId)
        return;

    m_bookId = bookId;
    emit bookIdChanged();
}

void NotesModel::reload() {
    QSqlDatabase &db = Database::forCurrentThread().connection();
    NoteRepository repo(db);
    const Result<QList<Note>> loaded = m_bookId > 0 ? repo.forBook(m_bookId) : repo.all();
    if (loaded.isErr()) {
        qWarning("could not list notes: %s", qUtf8Printable(loaded.error().message));
        return;
    }

    beginResetModel();
    m_notes = loaded.value();
    endResetModel();
    setCount(m_notes.size());
}

qint64 NotesModel::saveAnnotation(qint64 bookId, const QString &cfi, const QString &quote,
                                   const QString &body, double fraction) {
    NewNote note;
    note.bookId = bookId;
    note.cfi = nonEmpty(cfi);
    note.quote = nonEmpty(quote);
    // An empty body means "no body", not "erase the body" -- upsert()
    // COALESCEs a nullopt field against what is already stored, which is
    // what stops re-highlighting a passage from wiping a note someone wrote.
    note.body = nonEmpty(body);
    note.pageFraction = qBound(0.0, fraction, 1.0);

    QSqlDatabase &db = Database::forCurrentThread().connection();
    const Result<qint64> saved = NoteRepository(db).upsert(note);
    if (saved.isErr()) {
        qWarning("could not save annotation: %s", qUtf8Printable(saved.error().message));
        return 0;
    }

    reload();
    return saved.value();
}

void NotesModel::setBody(qint64 id, const QString &body) {
    QSqlDatabase &db = Database::forCurrentThread().connection();
    const Result<void> updated = NoteRepository(db).setBody(id, body);
    if (updated.isErr())
        qWarning("could not update note: %s", qUtf8Printable(updated.error().message));
    reload();
}

void NotesModel::remove(qint64 id) {
    QSqlDatabase &db = Database::forCurrentThread().connection();
    const Result<void> removed = NoteRepository(db).deleteNote(id);
    if (removed.isErr())
        qWarning("could not delete note: %s", qUtf8Printable(removed.error().message));
    reload();
}

QString NotesModel::annotationsJson(qint64 bookId) const {
    QSqlDatabase &db = Database::forCurrentThread().connection();
    const Result<QList<Note>> loaded = NoteRepository(db).forBook(bookId);
    if (loaded.isErr()) {
        qWarning("could not list annotations: %s", qUtf8Printable(loaded.error().message));
        return QStringLiteral("[]");
    }

    QJsonArray items;
    for (const Note &note : loaded.value()) {
        // Nothing on the page to anchor this to.
        if (!note.cfi.has_value() || note.cfi->isEmpty())
            continue;

        QJsonObject item;
        item.insert(QStringLiteral("id"), note.id);
        item.insert(QStringLiteral("cfi"), *note.cfi);
        item.insert(QStringLiteral("hasBody"), note.hasBody());
        items.append(item);
    }

    return QString::fromUtf8(QJsonDocument(items).toJson(QJsonDocument::Compact));
}
