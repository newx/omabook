// Storing and searching embeddings. Ported from
// omabook-core/src/ai/vectors.rs.
//
// Vectors are little-endian f32 blobs and similarity is cosine, computed
// here rather than through an ANN index -- see migration 004 for why.
#pragma once

#include "core/result.h"

#include <QByteArray>
#include <QPair>
#include <QSqlDatabase>
#include <QString>
#include <QVector>
#include <optional>

// Pack a vector for storage: little-endian float bytes, concatenated, no
// header. A deep copy -- never QByteArray::fromRawData over a vector that
// will go out of scope (CLAUDE.md, "Database").
QByteArray encode(const QVector<float> &vector);

// Unpack a stored vector. A blob whose length is not a multiple of four is
// corrupt, and saying so beats returning a silently truncated vector.
Result<QVector<float>> decode(const QByteArray &bytes);

// Cosine similarity, in -1.0..=1.0. Zero when either vector has no
// magnitude or the lengths disagree, which is the safe answer for
// "unrelated" -- never NaN, never a crash.
float cosine(const QVector<float> &a, const QVector<float> &b);

// A retrieved chunk and how well it matched.
struct Match {
    qint64 chunkId = 0;
    qint64 bookId = 0;
    qint64 ordinal = 0;
    QString text;
    float score = 0.0f;
};

// Reads and writes chunk_embeddings / book_embeddings, and searches them.
//
// Takes the caller's QSqlDatabase& rather than opening its own -- a
// QSqlDatabase connection belongs to one thread (CLAUDE.md, "Threading"),
// so the caller is the one who knows which connection that is.
class VectorStore {
public:
    explicit VectorStore(QSqlDatabase &db);

    Result<void> putChunk(qint64 chunkId, qint64 bookId, const QString &model, const QVector<float> &vector);
    Result<void> putBook(qint64 bookId, const QString &model, const QString &sourceHash,
                          const QVector<float> &vector);

    // Nearest chunks within one book. `upToOrdinal` bounds the search to
    // what the reader has already passed, which is what makes the "so far"
    // scope spoiler-free (SPEC §5.7).
    Result<QVector<Match>> nearestInBook(qint64 bookId, const QVector<float> &query, int limit,
                                          std::optional<qint64> upToOrdinal) const;

    // Vector and keyword results, blended. Each half contributes its own
    // best passages, so a question naming a thing and a question
    // describing an idea both retrieve well. Duplicates keep their higher
    // score.
    Result<QVector<Match>> hybridInBook(qint64 bookId, const QString &queryText,
                                         const QVector<float> &queryVector, int limit,
                                         std::optional<qint64> upToOrdinal) const;

    // Books whose metadata best matches the query.
    Result<QVector<QPair<qint64, float>>> nearestBooks(const QVector<float> &query, int limit) const;

    // How many of a book's chunks already have vectors: (embedded, total).
    Result<QPair<qint64, qint64>> chunkCoverage(qint64 bookId) const;

    // Terms for an FTS5 query, quoted so punctuation cannot be read as
    // syntax. Static and tested (CLAUDE.md, "Pure logic goes in static
    // member functions").
    static QString ftsTerms(const QString &query);

    // FTS5's `rank` is negative BM25; map it into roughly 0..1 so it can
    // sit alongside cosine scores. The exact curve matters less than the
    // ordering.
    static float normaliseRank(double rank);

private:
    // Passages found by keyword rather than meaning. Scores are normalised
    // into the same 0..1 range as cosine so the two result sets can be
    // blended.
    Result<QVector<Match>> keywordInBook(qint64 bookId, const QString &query, int limit,
                                          std::optional<qint64> upToOrdinal) const;

    QSqlDatabase &m_db;
};
