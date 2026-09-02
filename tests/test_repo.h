#pragma once

#include <QtTest>

#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <memory>

#include "core/db/database.h"
#include "core/models/book.h"
#include "core/repo/bookrepository.h"
#include "core/repo/noterepository.h"
#include "core/repo/settingsrepository.h"
#include "core/repo/taxonomy.h"
#include "core/result.h"

namespace {

// A NewBook with just enough set to import.
NewBook makeBook(const QString &title, const QString &sourcePath, const QString &fileHash,
                  const QStringList &authors = {}) {
    NewBook book;
    book.title = title;
    book.sourcePath = sourcePath;
    book.format = BookFormat::Epub;
    book.fileHash = fileHash;
    book.authors = authors;
    return book;
}

} // namespace

class RepoTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    // --- BookRepository: insert / find ----------------------------------

    void insertingTheSameHashTwiceDoesNotDuplicate() {
        auto db = Database::openForTest();
        BookRepository repo(db->connection());
        const NewBook book =
            makeBook(QStringLiteral("Zen"), QStringLiteral("/b/zen.epub"), QStringLiteral("same-hash"));

        const Result<qint64> first = repo.insert(book);
        const Result<qint64> second = repo.insert(book);

        QVERIFY(first.isOk());
        QVERIFY(second.isOk());
        QCOMPARE(second.value(), first.value());

        const Result<qint64> count = repo.count();
        QVERIFY(count.isOk());
        QCOMPARE(count.value(), qint64(1));
    }

    void authorsSurviveTheRoundTrip() {
        auto db = Database::openForTest();
        BookRepository repo(db->connection());
        const NewBook book =
            makeBook(QStringLiteral("Good Omens"), QStringLiteral("/b/go.epub"), QStringLiteral("h"),
                      {QStringLiteral("Pratchett"), QStringLiteral("Gaiman")});

        const Result<qint64> id = repo.insert(book);
        QVERIFY(id.isOk());

        const Result<std::optional<Book>> found = repo.find(id.value());
        QVERIFY(found.isOk());
        QVERIFY(found.value().has_value());
        QCOMPARE(found.value()->authors, QStringList({QStringLiteral("Pratchett"), QStringLiteral("Gaiman")}));
        QCOMPARE(found.value()->authorLine(), QStringLiteral("Pratchett, Gaiman"));
    }

    void sortingByTitleIsCaseInsensitive() {
        auto db = seededLibrary();
        BookRepository repo(db->connection());
        const Result<QList<Book>> books = repo.list(LibraryFilter::all(), BookSort::Title);
        QVERIFY(books.isOk());

        QStringList titles;
        for (const Book &book : books.value())
            titles << book.title;
        QCOMPARE(titles, QStringList({QStringLiteral("Atomic Bomb"), QStringLiteral("Zen")}));
    }

    // --- BookRepository: progress ----------------------------------------

    void firstProgressMovesTheBookToReading() {
        auto db = seededLibrary();
        BookRepository repo(db->connection());
        const qint64 id = repo.list(LibraryFilter::all(), BookSort::Title).value().first().id;

        QVERIFY(repo.saveProgress(id, QStringLiteral("epubcfi(/6/4)"), 0.10).isOk());

        const Book book = *repo.find(id).value();
        QCOMPARE(book.status, BookStatus::Reading);
        QVERIFY(qAbs(book.progress - 0.10) < 1e-9);
    }

    void passingTheThresholdFinishesTheBook() {
        auto db = seededLibrary();
        BookRepository repo(db->connection());
        const qint64 id = repo.list(LibraryFilter::all(), BookSort::Title).value().first().id;

        QVERIFY(repo.saveProgress(id, QStringLiteral("epubcfi(/6/4)"), 0.50).isOk());
        QCOMPARE(repo.find(id).value()->status, BookStatus::Reading);

        QVERIFY(repo.saveProgress(id, QStringLiteral("epubcfi(/6/900)"), COMPLETION_THRESHOLD).isOk());
        QCOMPARE(repo.find(id).value()->status, BookStatus::Finished);
    }

    void progressIsClampedRatherThanTrusted() {
        auto db = seededLibrary();
        BookRepository repo(db->connection());
        const qint64 id = repo.list(LibraryFilter::all(), BookSort::Title).value().first().id;

        QVERIFY(repo.saveProgress(id, QStringLiteral("x"), 42.0).isOk());
        QCOMPARE(repo.find(id).value()->progress, 1.0);

        QVERIFY(repo.saveProgress(id, QStringLiteral("x"), -3.0).isOk());
        QCOMPARE(repo.find(id).value()->progress, 0.0);
    }

    // --- BookRepository: search --------------------------------------------

    void anEmptyQueryReturnsNothingRatherThanEverything() {
        auto db = searchLibrary();
        BookRepository repo(db->connection());
        const Result<SearchResult> result = repo.search(QStringLiteral("   "), BookSort::Title);
        QVERIFY(result.isOk());
        QVERIFY(result.value().books.isEmpty());
        QCOMPARE(result.value().strategy, SearchStrategy::Empty);
    }

    void matchesAPartialWordAsYouType() {
        auto db = searchLibrary();
        BookRepository repo(db->connection());
        QCOMPARE(titlesOf(repo.search(QStringLiteral("mob"), BookSort::Title)),
                  QStringList({QStringLiteral("Moby-Dick")}));
    }

    void searchesAuthorsToo() {
        auto db = searchLibrary();
        BookRepository repo(db->connection());
        QCOMPARE(titlesOf(repo.search(QStringLiteral("melville"), BookSort::Title)),
                  QStringList({QStringLiteral("Moby-Dick")}));
    }

    void isCaseAndAccentInsensitive() {
        auto db = searchLibrary();
        BookRepository repo(db->connection());
        // remove_diacritics 2 means "CALCULO" finds "Cálculo".
        const QStringList found = titlesOf(repo.search(QStringLiteral("CALCULO"), BookSort::Title));
        QCOMPARE(found.size(), 1);
    }

    void allTermsMustMatch() {
        auto db = searchLibrary();
        BookRepository repo(db->connection());
        QCOMPARE(titlesOf(repo.search(QStringLiteral("discrete epp"), BookSort::Title)),
                  QStringList({QStringLiteral("Discrete Mathematics with Applications")}));
        QVERIFY(titlesOf(repo.search(QStringLiteral("discrete melville"), BookSort::Title)).isEmpty());
    }

    void punctuationDoesNotBreakTheQuery() {
        auto db = searchLibrary();
        BookRepository repo(db->connection());
        const QStringList queries = {
            QStringLiteral("\""),
            QStringLiteral("*"),
            QStringLiteral("moby-dick"),
            QStringLiteral("^("),
            QStringLiteral("AND"),
        };
        for (const QString &query : queries) {
            const Result<SearchResult> result = repo.search(query, BookSort::Title);
            QVERIFY2(result.isOk(), qPrintable(QStringLiteral("query %1 errored").arg(query)));
        }
    }

    void theIndexFollowsUpdatesAndDeletes() {
        auto db = searchLibrary();
        BookRepository repo(db->connection());

        {
            QSqlQuery query(db->connection());
            QVERIFY(
                query.exec(QStringLiteral("UPDATE books SET title = 'Billy Budd' WHERE title = 'Moby-Dick'")));
        }
        QVERIFY(titlesOf(repo.search(QStringLiteral("moby"), BookSort::Title)).isEmpty());
        QCOMPARE(titlesOf(repo.search(QStringLiteral("billy"), BookSort::Title)),
                  QStringList({QStringLiteral("Billy Budd")}));

        {
            QSqlQuery query(db->connection());
            QVERIFY(query.exec(QStringLiteral("DELETE FROM books WHERE title = 'Billy Budd'")));
        }
        QVERIFY(titlesOf(repo.search(QStringLiteral("billy"), BookSort::Title)).isEmpty());
    }

    // --- BookRepository: queue ---------------------------------------------

    void queueingIsAToggleAndSurvivesRemoval() {
        auto db = searchLibrary();
        BookRepository repo(db->connection());
        const qint64 id = repo.list(LibraryFilter::all(), BookSort::Title).value().first().id;

        QVERIFY(!repo.isQueued(id).value());
        QVERIFY(repo.toggleQueued(id).value());
        QVERIFY(repo.isQueued(id).value());
        QCOMPARE(repo.counts().value().queue, qint64(1));

        QVERIFY(!repo.toggleQueued(id).value());
        QVERIFY(!repo.isQueued(id).value());
        QCOMPARE(repo.counts().value().queue, qint64(0));

        // Re-adding after removal must work despite the partial unique index.
        QVERIFY(repo.toggleQueued(id).value());
    }

    void theQueueComesBackInTheOrderItWasGiven() {
        auto db = searchLibrary();
        BookRepository repo(db->connection());
        const QList<Book> all = repo.list(LibraryFilter::all(), BookSort::Title).value();
        for (const Book &book : all)
            QVERIFY(repo.toggleQueued(book.id).isOk());

        // Queued in title order; ask for the reverse.
        QList<qint64> reversed;
        for (auto it = all.rbegin(); it != all.rend(); ++it)
            reversed << it->id;
        QVERIFY(repo.reorderQueue(reversed).isOk());

        const QList<Book> queued = repo.list(LibraryFilter::queue(), BookSort::Title).value();
        QList<qint64> queuedIds;
        for (const Book &book : queued)
            queuedIds << book.id;
        QCOMPARE(queuedIds, reversed);

        // Removing one and reordering the rest leaves no gap behind.
        QVERIFY(repo.toggleQueued(reversed.first()).isOk());
        const QList<qint64> rest = reversed.mid(1);
        QVERIFY(repo.reorderQueue(rest).isOk());

        QList<qint64> positions;
        QSqlQuery query(db->connection());
        QVERIFY(query.exec(
            QStringLiteral("SELECT position FROM queue_items WHERE removed_at IS NULL ORDER BY position")));
        while (query.next())
            positions << query.value(0).toLongLong();
        QCOMPARE(positions, QList<qint64>({1, 2, 3}));
    }

    // --- BookRepository: delete cascades -----------------------------------

    void deletingABookTakesEverythingThatHangsOffIt() {
        auto db = Database::openForTest();
        const qint64 id = furnishedBook(*db);
        BookRepository repo(db->connection());

        // Everything is present before the delete, or the test proves nothing.
        QCOMPARE(countWhere(*db, QStringLiteral("reading_progress"), id), qint64(1));
        QCOMPARE(countWhere(*db, QStringLiteral("notes"), id), qint64(1));
        QCOMPARE(countWhere(*db, QStringLiteral("queue_items"), id), qint64(1));

        QVERIFY(repo.deleteBook(id).isOk());

        QVERIFY(!repo.find(id).value().has_value());
        const QStringList tables = {
            QStringLiteral("reading_progress"), QStringLiteral("notes"), QStringLiteral("queue_items"),
        };
        for (const QString &table : tables)
            QCOMPARE(countWhere(*db, table, id), qint64(0));
    }

    void deletingLeavesTheRestOfTheLibraryAlone() {
        auto db = Database::openForTest();
        const qint64 doomed = furnishedBook(*db);
        BookRepository repo(db->connection());
        const Result<qint64> keeper =
            repo.insert(makeBook(QStringLiteral("Billy Budd"), QStringLiteral("/b/b.epub"), QStringLiteral("h2")));
        QVERIFY(keeper.isOk());

        QVERIFY(repo.deleteBook(doomed).isOk());

        QVERIFY(repo.find(keeper.value()).value().has_value());
        QCOMPARE(repo.count().value(), qint64(1));
    }

    void deletingDropsTheBookOutOfSearch() {
        auto db = Database::openForTest();
        const qint64 id = furnishedBook(*db);
        BookRepository repo(db->connection());
        QCOMPARE(repo.search(QStringLiteral("moby"), BookSort::Title).value().books.size(), 1);

        QVERIFY(repo.deleteBook(id).isOk());
        QVERIFY(repo.search(QStringLiteral("moby"), BookSort::Title).value().books.isEmpty());
    }

    void deletingABookThatIsAlreadyGoneIsNotAnError() {
        auto db = Database::openForTest();
        BookRepository repo(db->connection());
        QVERIFY(repo.deleteBook(9999).isOk());
    }

    // --- NoteRepository ------------------------------------------------

    void aQuoteWithNoBodyIsAHighlight() {
        auto db = Database::openForTest();
        const qint64 id = insertBook(*db, QStringLiteral("Moby-Dick"), QStringLiteral("/b/m.epub"),
                                      QStringLiteral("h"));
        NoteRepository repo(db->connection());

        NewNote note;
        note.bookId = id;
        note.cfi = QStringLiteral("epubcfi(/6/4!/2)");
        note.quote = QStringLiteral("Call me Ishmael.");
        note.pageFraction = 0.02;
        QVERIFY(repo.upsert(note).isOk());

        const Result<QList<Note>> notes = repo.forBook(id);
        QVERIFY(notes.isOk());
        QCOMPARE(notes.value().size(), 1);
        QVERIFY(notes.value().first().isHighlightOnly());
        QVERIFY(!notes.value().first().hasBody());
    }

    void highlightingTheSamePassageTwiceUpdatesRatherThanDuplicates() {
        auto db = Database::openForTest();
        const qint64 id = insertBook(*db, QStringLiteral("Moby-Dick"), QStringLiteral("/b/m.epub"),
                                      QStringLiteral("h"));
        NoteRepository repo(db->connection());

        NewNote note;
        note.bookId = id;
        note.cfi = QStringLiteral("epubcfi(/6/4!/2)");
        note.quote = QStringLiteral("Call me Ishmael.");

        const Result<qint64> first = repo.upsert(note);
        const Result<qint64> second = repo.upsert(note);
        QVERIFY(first.isOk());
        QVERIFY(second.isOk());
        QCOMPARE(second.value(), first.value());
        QCOMPARE(repo.count().value(), qint64(1));
    }

    void reHighlightingDoesNotEraseAnExistingNoteBody() {
        auto db = Database::openForTest();
        const qint64 id = insertBook(*db, QStringLiteral("Moby-Dick"), QStringLiteral("/b/m.epub"),
                                      QStringLiteral("h"));
        NoteRepository repo(db->connection());
        const QString anchor = QStringLiteral("epubcfi(/6/4!/2)");

        NewNote withBody;
        withBody.bookId = id;
        withBody.cfi = anchor;
        withBody.quote = QStringLiteral("Call me Ishmael.");
        withBody.body = QStringLiteral("The most famous opening in English.");
        const Result<qint64> noteId = repo.upsert(withBody);
        QVERIFY(noteId.isOk());

        // The reader highlights the same passage again, carrying no body.
        NewNote reHighlight;
        reHighlight.bookId = id;
        reHighlight.cfi = anchor;
        reHighlight.quote = QStringLiteral("Call me Ishmael.");
        QVERIFY(repo.upsert(reHighlight).isOk());

        const Result<QList<Note>> notes = repo.forBook(id);
        QVERIFY(notes.isOk());
        QCOMPARE(notes.value().size(), 1);
        QCOMPARE(notes.value().first().id, noteId.value());
        QVERIFY2(notes.value().first().hasBody(), "the written note must survive");
    }

    void notesInABookComeBackInReadingOrder() {
        auto db = Database::openForTest();
        const qint64 id = insertBook(*db, QStringLiteral("Moby-Dick"), QStringLiteral("/b/m.epub"),
                                      QStringLiteral("h"));
        NoteRepository repo(db->connection());

        struct Entry {
            const char *cfi;
            double fraction;
            const char *quote;
        };
        const Entry entries[] = {
            {"c3", 0.9, "late"},
            {"c1", 0.1, "early"},
            {"c2", 0.5, "middle"},
        };
        for (const Entry &entry : entries) {
            NewNote note;
            note.bookId = id;
            note.cfi = QString::fromLatin1(entry.cfi);
            note.quote = QString::fromLatin1(entry.quote);
            note.pageFraction = entry.fraction;
            QVERIFY(repo.upsert(note).isOk());
        }

        const Result<QList<Note>> notes = repo.forBook(id);
        QVERIFY(notes.isOk());
        QStringList quotes;
        for (const Note &note : notes.value())
            quotes << *note.quote;
        QCOMPARE(quotes, QStringList({QStringLiteral("early"), QStringLiteral("middle"), QStringLiteral("late")}));
    }

    void anEmptyAnnotationIsRejected() {
        auto db = Database::openForTest();
        const qint64 id = insertBook(*db, QStringLiteral("Moby-Dick"), QStringLiteral("/b/m.epub"),
                                      QStringLiteral("h"));
        NoteRepository repo(db->connection());
        NewNote empty;
        empty.bookId = id;
        QVERIFY(repo.upsert(empty).isErr());
    }

    void deletingABookTakesItsNotesWithIt() {
        auto db = Database::openForTest();
        const qint64 id = insertBook(*db, QStringLiteral("Moby-Dick"), QStringLiteral("/b/m.epub"),
                                      QStringLiteral("h"));
        NoteRepository repo(db->connection());
        NewNote note;
        note.bookId = id;
        note.cfi = QStringLiteral("c");
        note.quote = QStringLiteral("q");
        QVERIFY(repo.upsert(note).isOk());

        {
            QSqlQuery query(db->connection());
            query.prepare(QStringLiteral("DELETE FROM books WHERE id = :id"));
            query.bindValue(QStringLiteral(":id"), id);
            QVERIFY(query.exec());
        }
        QCOMPARE(repo.count().value(), qint64(0));
    }

    // --- SettingsRepository ----------------------------------------------

    void settingsRoundTripAndOverwrite() {
        auto db = Database::openForTest();
        SettingsRepository repo(db->connection());

        const Result<std::optional<QString>> initial = repo.get(QStringLiteral("theme"));
        QVERIFY(initial.isOk());
        QVERIFY(!initial.value().has_value());
        QCOMPARE(repo.getOr(QStringLiteral("theme"), QStringLiteral("system")), QStringLiteral("system"));

        QVERIFY(repo.set(QStringLiteral("theme"), QStringLiteral("dark")).isOk());
        QCOMPARE(repo.getOr(QStringLiteral("theme"), QStringLiteral("system")), QStringLiteral("dark"));

        QVERIFY(repo.set(QStringLiteral("theme"), QStringLiteral("light")).isOk());
        QCOMPARE(repo.getOr(QStringLiteral("theme"), QStringLiteral("system")), QStringLiteral("light"));
    }

    // --- taxonomy --------------------------------------------------------

    void slugsNormalisePunctuationAndCase() {
        QCOMPARE(slugify(QStringLiteral("Science Fiction")), QStringLiteral("science-fiction"));
        QCOMPARE(slugify(QStringLiteral("Children -- Books and reading")),
                  QStringLiteral("children-books-and-reading"));
        QCOMPARE(slugify(QStringLiteral("  Sci-Fi!  ")), QStringLiteral("sci-fi"));
        QCOMPARE(slugify(QStringLiteral("C++")), QStringLiteral("c"));
    }

    void ensureIsIdempotentAcrossSpellings() {
        auto db = Database::openForTest();
        CategoryRepository repo(db->connection());
        const Result<qint64> a = repo.ensure(QStringLiteral("Science Fiction"));
        const Result<qint64> b = repo.ensure(QStringLiteral("science fiction"));
        QVERIFY(a.isOk());
        QVERIFY(b.isOk());
        QCOMPARE(b.value(), a.value());
    }

    void emptyCategoriesAreNotListed() {
        auto db = Database::openForTest();
        CategoryRepository repo(db->connection());
        QVERIFY(repo.ensure(QStringLiteral("Orphaned")).isOk());
        QVERIFY(repo.listWithCounts().value().isEmpty());
    }

    void countsReflectAssignedBooks() {
        auto db = Database::openForTest();
        BookRepository books(db->connection());
        CategoryRepository cats(db->connection());
        TagRepository tags(db->connection());

        const Result<qint64> id =
            books.insert(makeBook(QStringLiteral("Dune"), QStringLiteral("/b/d.epub"), QStringLiteral("h1")));
        QVERIFY(id.isOk());
        const Result<qint64> cat = cats.ensure(QStringLiteral("Fiction"));
        QVERIFY(cat.isOk());
        QVERIFY(cats.assign(id.value(), cat.value()).isOk());

        const Result<qint64> tag = tags.ensure(QStringLiteral("Sci-Fi"));
        QVERIFY(tag.isOk());
        QVERIFY(tags.attach(id.value(), tag.value(), QStringLiteral("manual")).isOk());
        QVERIFY(tags.attach(id.value(), tag.value(), QStringLiteral("manual")).isOk()); // repeat, no dup

        QCOMPARE(cats.listWithCounts().value().first().bookCount, qint64(1));
        QCOMPARE(tags.listWithCounts().value().first().bookCount, qint64(1));
    }

private:
    // Two books, so title-sort has something to prove.
    std::unique_ptr<Database> seededLibrary() {
        auto db = Database::openForTest();
        BookRepository repo(db->connection());
        repo.insert(makeBook(QStringLiteral("Zen"), QStringLiteral("/b/zen.epub"), QStringLiteral("h1"),
                              {QStringLiteral("Pirsig")}));
        repo.insert(makeBook(QStringLiteral("Atomic Bomb"), QStringLiteral("/b/bomb.pdf"), QStringLiteral("h2"),
                              {QStringLiteral("Rhodes")}));
        return db;
    }

    // Four books tuned for the search tests: a partial-word match, an
    // author match, a diacritic, and a two-word conjunction.
    std::unique_ptr<Database> searchLibrary() {
        auto db = Database::openForTest();
        BookRepository repo(db->connection());
        struct Entry {
            const char *title;
            const char *author;
            const char *hash;
        };
        const Entry entries[] = {
            {"Moby-Dick", "Herman Melville", "h1"},
            {"Discrete Mathematics with Applications", "Susanna S. Epp", "h2"},
            {"The Waste Land", "T.S. Eliot", "h3"},
            {"Pré-Cálculo: Uma Preparação para o Cálculo", "Sheldon Axler", "h4"},
        };
        for (const Entry &entry : entries) {
            const QString hash = QString::fromUtf8(entry.hash);
            const NewBook book = makeBook(QString::fromUtf8(entry.title), QStringLiteral("/b/%1.epub").arg(hash),
                                           hash, {QString::fromUtf8(entry.author)});
            repo.insert(book);
        }
        return db;
    }

    static QStringList titlesOf(const Result<SearchResult> &result) {
        QStringList titles;
        if (!result.isOk())
            return titles;
        for (const Book &book : result.value().books)
            titles << book.title;
        return titles;
    }

    static qint64 insertBook(Database &db, const QString &title, const QString &sourcePath,
                              const QString &hash) {
        BookRepository repo(db.connection());
        return repo.insert(makeBook(title, sourcePath, hash)).value();
    }

    // A book with something hanging off every table that references it. The
    // embedding row is inserted with raw SQL rather than through the vector
    // store, which is a different subsystem.
    static qint64 furnishedBook(Database &db) {
        BookRepository repo(db.connection());
        const qint64 id =
            repo.insert(makeBook(QStringLiteral("Moby-Dick"), QStringLiteral("/b/m.epub"), QStringLiteral("h1")))
                .value();

        repo.saveProgress(id, QStringLiteral("epubcfi(/6/4)"), 0.4);
        repo.toggleQueued(id);

        NoteRepository notes(db.connection());
        NewNote note;
        note.bookId = id;
        note.cfi = QStringLiteral("epubcfi(/6/4)");
        note.quote = QStringLiteral("Call me Ishmael.");
        notes.upsert(note);

        // Chunks and embeddings used to be seeded here too. Migration 006
        // dropped those tables with the assistant.

        return id;
    }

    static qint64 countWhere(Database &db, const QString &table, qint64 bookId) {
        QSqlQuery query(db.connection());
        query.prepare(QStringLiteral("SELECT COUNT(*) FROM %1 WHERE book_id = :id").arg(table));
        query.bindValue(QStringLiteral(":id"), bookId);
        query.exec();
        return query.next() ? query.value(0).toLongLong() : qint64(-1);
    }
};
