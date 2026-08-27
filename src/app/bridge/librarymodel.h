// LibraryModel -- the books, as a Qt list model.
//
// Its only job is translation: Book values become model roles. No queries
// live here; those belong to BookRepository (SPEC §2.5). Ported from
// omabook-app/src/bridge/library.rs.
#pragma once

#include "core/models/book.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QSet>
#include <QString>

class QThread;
class ImportWorker;

class LibraryModel : public QAbstractListModel {
    Q_OBJECT
    // Snake case, and deliberately: the QML reads `library.status_line`,
    // because cxx-qt never camelCased a multi-word qproperty and the ported
    // QML carries those names byte-for-byte. The C++ accessors stay
    // camelCase; only the QML-visible token differs (CLAUDE.md, "QML
    // conventions"). Get this wrong and the binding reads `undefined` with
    // no warning at any stage.
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString status_line READ statusLine NOTIFY statusLineChanged)
    Q_PROPERTY(QString filter READ filter NOTIFY filterChanged)
    Q_PROPERTY(QString search READ search NOTIFY searchChanged)
    Q_PROPERTY(QString search_strategy READ searchStrategy NOTIFY searchStrategyChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    // Roles from Qt::UserRole (256), in this fixed order -- the Rust
    // bridge's role module, ported verbatim so a role's numeric value never
    // shifts under an existing delegate binding.
    enum Role {
        TitleRole = Qt::UserRole,
        AuthorRole,
        ProgressRole,
        StatusRole,
        FormatRole,
        BookIdRole,
        CoverUrlRole,
        IsFavoriteRole,
        IsQueuedRole,
    };

    explicit LibraryModel(QObject *parent = nullptr);
    ~LibraryModel() override;

    int count() const { return m_count; }
    QString statusLine() const { return m_statusLine; }
    QString filter() const { return m_filter; }
    QString search() const { return m_search; }
    QString searchStrategy() const { return m_searchStrategy; }
    bool busy() const { return m_busy; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Re-read the current filter's (or search's, or ranked list's) books
    // from the database and reset the model.
    Q_INVOKABLE void reload();

    // Change which slice of the library is shown, then reload. Accepts
    // "all", "favorites", "reading", "queue", "completed", "category:<id>"
    // or "tag:<id>". Clears any active search or ranked answer, otherwise
    // the click would appear to do nothing.
    Q_INVOKABLE void setFilterAndReload(const QString &filter);

    // Search the library. An empty query returns to the current filter.
    Q_INVOKABLE void setSearchAndReload(const QString &query);

    // Show exactly these books, in exactly this order -- the result of a
    // library question, where the ranking is the answer. Drives
    // AiController::libraryAnswered.
    Q_INVOKABLE void showRankedBooks(const QString &ids);

    Q_INVOKABLE void toggleFavorite(qint64 bookId);
    Q_INVOKABLE void toggleQueued(qint64 bookId);

    // Remove a book from the library. The file on disk is untouched. Also
    // drops the id from any active ranked list, which would otherwise go on
    // showing a book that no longer exists.
    Q_INVOKABLE void deleteBook(qint64 bookId);

    // Move a queued book from one place in the grid to another, and
    // persist the new order. Acts only while the raw Queue view is
    // showing, unfiltered and unsearched -- every other list is
    // database-ordered and a drag there would be silently undone on the
    // next reload.
    Q_INVOKABLE void moveQueued(int from, int to);

    // Persist reader position. Called from the reader bridge.
    Q_INVOKABLE void saveProgress(qint64 bookId, const QString &position, double fraction);

    // The absolute file path the reader should open for a book.
    Q_INVOKABLE QString readingPathFor(qint64 bookId) const;
    Q_INVOKABLE QString titleFor(qint64 bookId) const;

    // How usable a book's text is: "good", "poor", "none", or "" when
    // unknown. Gates the reading-aloud controls (SPEC §7.2).
    Q_INVOKABLE QString textQualityFor(qint64 bookId) const;

    // Text of one page of a PDF, via pdftotext. Fixed-layout PDFs render to
    // canvas and expose no text to the page, so the reader cannot supply
    // it; this is how TTS gets text for half the library.
    Q_INVOKABLE QString pdfPageText(qint64 bookId, int page) const;

    // The reader page URL for a book, resuming where reading stopped.
    Q_INVOKABLE QString readerUrlFor(qint64 bookId) const;

    // Import every book under `path`, on a worker thread. The window stays
    // responsive; busy and status_line report progress. A no-op while
    // already busy.
    Q_INVOKABLE void importDirectory(const QString &path);

signals:
    void countChanged();
    void statusLineChanged();
    void filterChanged();
    void searchChanged();
    void searchStrategyChanged();
    void busyChanged();

private slots:
    void onImportProgress(const QString &text);
    void onImportFinished(const QString &message);

private:
    void setCount(int count);
    void setStatusLine(const QString &statusLine);
    void setFilter(const QString &filter);
    void setSearch(const QString &search);
    void setSearchStrategy(const QString &strategy);
    void setBusy(bool busy);

    QList<Book> m_books;
    // Ids currently in the reading queue, so the grid can show it per book
    // without a query per row -- built once per reload, not per row.
    QSet<qint64> m_queued;
    // Ids from a library question. Non-empty means the grid is showing an
    // answer rather than a filter.
    QList<qint64> m_ranked;

    int m_count = 0;
    QString m_statusLine;
    QString m_filter = QStringLiteral("all");
    QString m_search;
    QString m_searchStrategy;
    bool m_busy = false;

    // The most recently started import thread, tracked only so the
    // destructor can quit() and wait() it if the app closes mid-import --
    // never touched from any thread but this one. A QPointer so it reads
    // null on its own once the thread has deleted itself, rather than
    // dangling.
    QPointer<QThread> m_importThread;
};
