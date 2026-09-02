// LibraryModel -- the books, as a Qt list model.
//
// Its only job is translation: Book values become model roles. No queries
// live here; those belong to BookRepository (SPEC §2.5).
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
    // Snake case, and deliberately: the QML reads `library.status_line`, and
    // the QML is the contract. The C++ accessors stay camelCase; only the
    // QML-visible token differs (CLAUDE.md, "QML conventions"). Get this wrong
    // and the binding reads `undefined` with no warning at any stage.
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString status_line READ statusLine NOTIFY statusLineChanged)
    Q_PROPERTY(QString filter READ filter NOTIFY filterChanged)
    Q_PROPERTY(QString search READ search NOTIFY searchChanged)
    Q_PROPERTY(QString search_strategy READ searchStrategy NOTIFY searchStrategyChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // Snake case, because the QML reads library_folder -- see CLAUDE.md,
    // "QML conventions".
    Q_PROPERTY(QString library_folder READ libraryFolder NOTIFY libraryFolderChanged)

public:
    // Roles from Qt::UserRole (256), in this fixed order, so a role's numeric
    // value never shifts under an existing delegate binding.
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
        TextQualityRole,
    };

    explicit LibraryModel(QObject *parent = nullptr);
    ~LibraryModel() override;

    int count() const { return m_count; }
    QString statusLine() const { return m_statusLine; }
    QString filter() const { return m_filter; }
    QString search() const { return m_search; }
    QString searchStrategy() const { return m_searchStrategy; }
    bool busy() const { return m_busy; }
    QString libraryFolder() const { return m_libraryFolder; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Re-read the current filter's (or search's) books from the database
    // and reset the model.
    Q_INVOKABLE void reload();

    // Change which slice of the library is shown, then reload. Accepts
    // "all", "favorites", "reading", "queue", "completed", "category:<id>"
    // or "tag:<id>". Clears any active search, otherwise the click would
    // appear to do nothing.
    Q_INVOKABLE void setFilterAndReload(const QString &filter);

    // Search the library. An empty query returns to the current filter.
    Q_INVOKABLE void setSearchAndReload(const QString &query);

    Q_INVOKABLE void toggleFavorite(qint64 bookId);
    Q_INVOKABLE void toggleQueued(qint64 bookId);

    // Remove a book from the library. The file on disk is untouched.
    Q_INVOKABLE void deleteBook(qint64 bookId);

    // Move a queued book from one place in the grid to another, and
    // persist the new order. Acts only while the raw Queue view is
    // showing, unfiltered and unsearched -- every other list is
    // database-ordered and a drag there would be silently undone on the
    // next reload.
    Q_INVOKABLE void moveQueued(int from, int to);

    // Remember the folder for future imports and persist it. Called from
    // Settings; also called internally so a folder just imported is the one
    // imported again by default.
    Q_INVOKABLE void setLibraryFolder(const QString &path);

    // Persist reader position. Called from the reader bridge.
    Q_INVOKABLE void saveProgress(qint64 bookId, const QString &position, double fraction);

    // The absolute file path the reader should open for a book.
    Q_INVOKABLE QString readingPathFor(qint64 bookId) const;
    Q_INVOKABLE QString titleFor(qint64 bookId) const;

    // How usable a book's text is: "good", "poor", "none", or "" when
    // unknown. Gates the reading-aloud controls (SPEC §7.2).
    Q_INVOKABLE QString textQualityFor(qint64 bookId) const;

    // The reader page URL for a book, resuming where reading stopped.
    // `targetCfi` empty means "wherever I left off"; anything else is a
    // deliberate destination -- a highlight or a note being opened from the
    // Highlights view, which must land on the passage rather than on the
    // reading position.
    Q_INVOKABLE QString readerUrlFor(qint64 bookId,
                                     const QString &targetCfi = QString()) const;

    // Import every book under `path`, on a worker thread. The window stays
    // responsive; busy and status_line report progress. A no-op while
    // already busy. Remembers `path` as library_folder, so the no-argument
    // overload below re-imports the same place.
    Q_INVOKABLE void importDirectory(const QString &path);

    // Import the remembered library_folder. Sets status_line and does
    // nothing else when no folder has been remembered yet.
    Q_INVOKABLE void importDirectory();

signals:
    void countChanged();
    void statusLineChanged();
    void filterChanged();
    void searchChanged();
    void searchStrategyChanged();
    void busyChanged();
    void libraryFolderChanged();

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

    int m_count = 0;
    QString m_statusLine;
    QString m_filter = QStringLiteral("all");
    QString m_search;
    QString m_searchStrategy;
    bool m_busy = false;
    QString m_libraryFolder;

    // The most recently started import thread, tracked only so the
    // destructor can quit() and wait() it if the app closes mid-import --
    // never touched from any thread but this one. A QPointer so it reads
    // null on its own once the thread has deleted itself, rather than
    // dangling.
    QPointer<QThread> m_importThread;
};
