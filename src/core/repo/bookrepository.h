// Reading and writing books, ported from omabook-core/src/repo/book_repo.rs.
#pragma once

#include "core/models/book.h"
#include "core/result.h"

#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <optional>

// Which slice of the library to show. Mirrors the sidebar (SPEC §5.1).
//
// Rust spells this as an enum with data on two variants
// (Category(i64)/Tag(i64)); a C++ enum cannot carry a payload, so this is a
// small struct instead -- `id` is meaningful only for Category and Tag.
struct LibraryFilter {
    enum class Kind { All, Favorites, Reading, Queue, Completed, Category, Tag };

    Kind kind = Kind::All;
    qint64 id = 0;

    static LibraryFilter all() { return LibraryFilter{Kind::All, 0}; }
    static LibraryFilter favorites() { return LibraryFilter{Kind::Favorites, 0}; }
    static LibraryFilter reading() { return LibraryFilter{Kind::Reading, 0}; }
    static LibraryFilter queue() { return LibraryFilter{Kind::Queue, 0}; }
    static LibraryFilter completed() { return LibraryFilter{Kind::Completed, 0}; }
    static LibraryFilter category(qint64 categoryId) { return LibraryFilter{Kind::Category, categoryId}; }
    static LibraryFilter tag(qint64 tagId) { return LibraryFilter{Kind::Tag, tagId}; }
};

// How a library listing is ordered.
enum class BookSort { RecentlyAdded, Title, Author, Progress };

// How a text query was matched. Semantic search arrives in Phase 6, once
// embeddings exist; until then this is the full-text index (SPEC §5.1).
enum class SearchStrategy { FullText, Substring, Empty };

// Sidebar badge counts, computed in one pass.
struct FilterCounts {
    qint64 all = 0;
    qint64 favorites = 0;
    qint64 reading = 0;
    qint64 queue = 0;
    qint64 completed = 0;
};

// The matches plus how they were found -- a search box may want to tell the
// user their query fell back to a plain substring match.
struct SearchResult {
    QList<Book> books;
    SearchStrategy strategy = SearchStrategy::Empty;
};

// Where a book counts as read to the end. SPEC §5.8 -- books rarely reach a
// true 1.0 because of trailing matter.
constexpr double COMPLETION_THRESHOLD = 0.98;

// Reading and writing the books table and everything joined to it.
//
// Takes a QSqlDatabase & rather than owning one, mirroring the Rust type's
// `&'a Connection`: a connection belongs to one thread (CLAUDE.md,
// "Threading"), so this repository must never outlive the call that
// constructed it, and must never copy the reference into a stored
// QSqlDatabase.
class BookRepository {
public:
    explicit BookRepository(QSqlDatabase &db) : m_db(db) { }

    // Every non-archived book matching `filter`, in `sort` order -- except
    // the Queue filter, which always uses its own hand-ordered sequence.
    Result<QList<Book>> list(const LibraryFilter &filter, BookSort sort) const;

    // Exactly these books, in exactly this order -- used where the id
    // ordering itself is the answer and re-sorting would discard it.
    Result<QList<Book>> listIds(const QList<qint64> &ids) const;

    // Free-text search. FTS5 first; any parse error falls back to a
    // substring match rather than surfacing, and is reported through
    // `SearchResult::strategy` rather than distinguishing success/failure.
    Result<SearchResult> search(const QString &query, BookSort sort) const;

    Result<bool> isQueued(qint64 bookId) const;
    // Add to the end of the queue, or remove if already there. Returns the
    // new state.
    Result<bool> toggleQueued(qint64 bookId);
    // Rewrite the queue's order to exactly this sequence of book ids,
    // renumbering positions gaplessly from 1.
    Result<void> reorderQueue(const QList<qint64> &ordered);

    // Remove a book and everything the schema cascades from it. Named
    // deleteBook, not delete, because the latter is a C++ keyword.
    Result<void> deleteBook(qint64 bookId);
    Result<bool> toggleFavorite(qint64 bookId);
    Result<FilterCounts> counts() const;

    Result<std::optional<Book>> find(qint64 id) const;
    // The lookup that makes re-scanning idempotent (SPEC §5.2).
    Result<std::optional<Book>> findByHash(const QString &fileHash) const;
    Result<qint64> count() const;

    // Insert a book, returning its id. Returns the existing id when a book
    // with the same content hash is already present, so a re-scan is a
    // no-op.
    Result<qint64> insert(const NewBook &book);

    // Record where the reader got to. Also advances unread -> reading on
    // first sight, and -> finished past COMPLETION_THRESHOLD (SPEC §5.8).
    Result<void> saveProgress(qint64 bookId, const QString &position, double fraction);

    // Re-record how usable a book's text is. Import is idempotent by
    // content hash, so a smarter assessment does not reach books already
    // imported -- they have to be refreshed deliberately.
    Result<void> setTextQuality(qint64 bookId, TextQuality quality);

    // Where the reader stopped, if it ever started.
    Result<std::optional<QString>> progressPosition(qint64 bookId) const;

    // Turns free text into an FTS5 MATCH expression: each whitespace-
    // separated term is stripped to alphanumerics and apostrophes, empty
    // terms dropped, and the rest quoted and prefix-starred so "moby" finds
    // "Moby-Dick" before the word is complete, and punctuation in a title
    // cannot be read as FTS operator syntax. Static and tested on its own
    // (CLAUDE.md, "Pure logic goes in static member functions").
    static QString ftsQuery(const QString &query);

private:
    Result<QList<Book>> searchFts(const QString &query, BookSort sort) const;
    Result<QList<Book>> searchSubstring(const QString &query, BookSort sort) const;

    QSqlDatabase &m_db;
};
