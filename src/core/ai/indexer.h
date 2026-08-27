// Preparing a book for retrieval: split it into chunks, then embed them.
// Ported from omabook-core/src/ai/indexer.rs.
//
// This is the only AI work that may run unattended, and only under
// WorkPolicy -- local provider, opt-in, on mains power (SPEC §5.5). It is
// also resumable: chunks already embedded are skipped, so a cancelled run
// costs nothing to repeat.
#pragma once

#include "core/ai/provider.h"
#include "core/models/book.h"
#include "core/result.h"

#include <QSqlDatabase>
#include <QString>
#include <QStringList>

#include <functional>

namespace Indexer {

// Roughly a paragraph or two: small enough to be a precise answer, large
// enough to carry context.
constexpr int TARGET_CHUNK_CHARS = 1200;
constexpr int MIN_CHUNK_CHARS = 120;

// Progress from one book's preparation. chunksWritten is left to whichever
// caller combines chunkBook() and embedBook() into one report -- neither
// function here sets it, matching the Rust original.
struct IndexReport {
    int chunksWritten = 0;
    int chunksEmbedded = 0;
    int alreadyDone = 0;
};

// Split a book's text into chunks and store them. Idempotent: a book that
// already has rows in book_chunks is left alone, and its existing chunk
// count is returned rather than re-splitting.
Result<int> chunkBook(QSqlDatabase &db, qint64 bookId);

// Embed a book's chunks, skipping any already done. `shouldContinue` is
// checked before each chunk so a long run can be stopped promptly --
// closing the app or unplugging the power should not mean waiting out a
// whole textbook. One chunk that fails to embed logs a warning and the book
// continues; it does not abort the run.
Result<IndexReport> embedBook(QSqlDatabase &db, qint64 bookId, EmbedProvider &embedder,
                               std::function<void(int, int)> progress,
                               std::function<bool()> shouldContinue);

// Embed the book-level vector used by library-wide questions. Cheap: one
// call per book over its metadata. Skipped when the same metadata was
// already embedded, tracked by book_embeddings.source_hash.
Result<bool> embedBookMetadata(QSqlDatabase &db, qint64 bookId, EmbedProvider &embedder);

// --- Pure logic below, testable without a database -------------------------

// Split on blank lines, packing paragraphs greedily up to the target size.
// Paragraphs are the natural unit: splitting mid-sentence produces chunks
// that retrieve well but read badly when shown as evidence. A free function
// (CLAUDE.md, "Pure logic goes in static member functions") so the packing
// rules are reachable from a test with no database.
QStringList split(const QString &text);

// What a book "is", in words, for the library-level vector: title; authors;
// the opening of the description; the category name; the tag names --
// joined with ". ". Needs the database only to look up the category and tag
// names, which are not carried on Book itself.
Result<QString> metadataText(QSqlDatabase &db, const Book &book);

// The first 16 hex characters of the text's SHA-256, used to tell whether a
// book's metadata changed since it was last embedded.
QString shortHash(const QString &text);

} // namespace Indexer
