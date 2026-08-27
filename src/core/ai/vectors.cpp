#include "core/ai/vectors.h"

#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// Sort by score, best first, and keep the top `limit`.
void rankAndTruncate(QVector<Match> &matches, int limit) {
    std::sort(matches.begin(), matches.end(),
              [](const Match &a, const Match &b) { return a.score > b.score; });
    if (matches.size() > limit)
        matches.resize(limit);
}

} // namespace

QByteArray encode(const QVector<float> &vector) {
    QByteArray bytes;
    bytes.resize(vector.size() * static_cast<int>(sizeof(float)));
    // A deep copy into memory we own -- never alias a QByteArray's data as
    // a float*, its alignment is not guaranteed (CLAUDE.md, "Database").
    // Little-endian on the host, which is every platform this app ships on.
    if (!vector.isEmpty())
        std::memcpy(bytes.data(), vector.constData(), static_cast<size_t>(bytes.size()));
    return bytes;
}

Result<QVector<float>> decode(const QByteArray &bytes) {
    if (bytes.size() % static_cast<int>(sizeof(float)) != 0) {
        return Error::decode(QStringLiteral("embedding blob is %1 bytes, not a whole number of f32")
                                  .arg(bytes.size()));
    }

    QVector<float> vector(bytes.size() / static_cast<int>(sizeof(float)));
    if (!vector.isEmpty())
        std::memcpy(vector.data(), bytes.constData(), static_cast<size_t>(bytes.size()));
    return Result<QVector<float>>::ok(vector);
}

float cosine(const QVector<float> &a, const QVector<float> &b) {
    if (a.size() != b.size() || a.isEmpty())
        return 0.0f;

    const float *pa = a.constData();
    const float *pb = b.constData();
    const int n = a.size();

    // Three plain loops, each with a single accumulator and no branches in
    // the body -- what GCC auto-vectorizes best at -O3 (CLAUDE.md).
    float dot = 0.0f;
    for (int i = 0; i < n; ++i)
        dot += pa[i] * pb[i];

    float normA = 0.0f;
    for (int i = 0; i < n; ++i)
        normA += pa[i] * pa[i];

    float normB = 0.0f;
    for (int i = 0; i < n; ++i)
        normB += pb[i] * pb[i];

    const float magnitude = std::sqrt(normA) * std::sqrt(normB);
    if (magnitude == 0.0f)
        return 0.0f;
    return dot / magnitude;
}

VectorStore::VectorStore(QSqlDatabase &db) : m_db(db) { }

Result<void> VectorStore::putChunk(qint64 chunkId, qint64 bookId, const QString &model,
                                    const QVector<float> &vector) {
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO chunk_embeddings (chunk_id, book_id, model, dims, vector) "
        "VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(chunk_id) DO UPDATE SET "
        "   model = excluded.model, dims = excluded.dims, vector = excluded.vector"));
    query.addBindValue(chunkId);
    query.addBindValue(bookId);
    query.addBindValue(model);
    query.addBindValue(vector.size());
    query.addBindValue(encode(vector));

    if (!query.exec())
        return Error::db(query.lastError().text());
    return VoidResult::ok();
}

Result<void> VectorStore::putBook(qint64 bookId, const QString &model, const QString &sourceHash,
                                   const QVector<float> &vector) {
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO book_embeddings (book_id, model, dims, vector, source_hash) "
        "VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(book_id) DO UPDATE SET "
        "   model = excluded.model, dims = excluded.dims, "
        "   vector = excluded.vector, source_hash = excluded.source_hash"));
    query.addBindValue(bookId);
    query.addBindValue(model);
    query.addBindValue(vector.size());
    query.addBindValue(encode(vector));
    query.addBindValue(sourceHash);

    if (!query.exec())
        return Error::db(query.lastError().text());
    return VoidResult::ok();
}

Result<QVector<Match>> VectorStore::nearestInBook(qint64 bookId, const QVector<float> &query,
                                                   int limit, std::optional<qint64> upToOrdinal) const {
    QVector<Match> matches;
    const qint64 bound = upToOrdinal.value_or(-1);

    {
        QSqlQuery sql(m_db);
        sql.prepare(QStringLiteral(
            "SELECT e.chunk_id, e.book_id, c.ordinal, c.text, e.vector "
            "  FROM chunk_embeddings e "
            "  JOIN book_chunks c ON c.id = e.chunk_id "
            " WHERE e.book_id = ? AND (? < 0 OR c.ordinal <= ?)"));
        sql.addBindValue(bookId);
        sql.addBindValue(bound);
        sql.addBindValue(bound);

        if (!sql.exec())
            return Error::db(sql.lastError().text());

        while (sql.next()) {
            const QByteArray blob = sql.value(4).toByteArray();
            const Result<QVector<float>> vector = decode(blob);
            if (vector.isErr())
                return vector.error();

            Match match;
            match.chunkId = sql.value(0).toLongLong();
            match.bookId = sql.value(1).toLongLong();
            match.ordinal = sql.value(2).toLongLong();
            match.text = sql.value(3).toString();
            match.score = cosine(query, vector.value());
            matches.append(match);
        }
    }

    rankAndTruncate(matches, limit);
    return Result<QVector<Match>>::ok(matches);
}

Result<QVector<Match>> VectorStore::keywordInBook(qint64 bookId, const QString &queryText, int limit,
                                                   std::optional<qint64> upToOrdinal) const {
    const QString terms = ftsTerms(queryText);
    if (terms.isEmpty())
        return Result<QVector<Match>>::ok(QVector<Match>());

    const qint64 bound = upToOrdinal.value_or(-1);

    QVector<Match> matches;
    QSqlQuery sql(m_db);
    sql.prepare(QStringLiteral(
        "SELECT c.id, c.book_id, c.ordinal, c.text, chunks_fts.rank "
        "  FROM chunks_fts "
        "  JOIN book_chunks c ON c.id = chunks_fts.rowid "
        " WHERE chunks_fts MATCH ? "
        "   AND c.book_id = ? "
        "   AND (? < 0 OR c.ordinal <= ?) "
        " ORDER BY chunks_fts.rank "
        " LIMIT ?"));
    sql.addBindValue(terms);
    sql.addBindValue(bookId);
    sql.addBindValue(bound);
    sql.addBindValue(bound);
    sql.addBindValue(limit);

    if (!sql.exec())
        return Error::db(sql.lastError().text());

    while (sql.next()) {
        Match match;
        match.chunkId = sql.value(0).toLongLong();
        match.bookId = sql.value(1).toLongLong();
        match.ordinal = sql.value(2).toLongLong();
        match.text = sql.value(3).toString();
        match.score = normaliseRank(sql.value(4).toDouble());
        matches.append(match);
    }

    return Result<QVector<Match>>::ok(matches);
}

Result<QVector<Match>> VectorStore::hybridInBook(qint64 bookId, const QString &queryText,
                                                  const QVector<float> &queryVector, int limit,
                                                  std::optional<qint64> upToOrdinal) const {
    Result<QVector<Match>> vectorResult = nearestInBook(bookId, queryVector, limit, upToOrdinal);
    if (vectorResult.isErr())
        return vectorResult.error();
    QVector<Match> combined = vectorResult.value();

    Result<QVector<Match>> keywordResult = keywordInBook(bookId, queryText, limit, upToOrdinal);
    if (keywordResult.isErr())
        return keywordResult.error();

    // Keyword hits are weighted a little below semantic ones: they are a
    // corrective, not the primary signal.
    for (const Match &hit : keywordResult.value()) {
        const float weighted = hit.score * 0.85f;
        auto existing = std::find_if(combined.begin(), combined.end(),
                                      [&hit](const Match &m) { return m.chunkId == hit.chunkId; });
        if (existing != combined.end()) {
            existing->score = std::max(existing->score, weighted);
        } else {
            Match weightedHit = hit;
            weightedHit.score = weighted;
            combined.append(weightedHit);
        }
    }

    rankAndTruncate(combined, limit);
    return Result<QVector<Match>>::ok(combined);
}

Result<QVector<QPair<qint64, float>>> VectorStore::nearestBooks(const QVector<float> &query, int limit) const {
    QVector<QPair<qint64, float>> scored;

    QSqlQuery sql(m_db);
    if (!sql.exec(QStringLiteral(
            "SELECT e.book_id, e.vector "
            "  FROM book_embeddings e "
            "  JOIN books b ON b.id = e.book_id "
            " WHERE b.archived_at IS NULL"))) {
        return Error::db(sql.lastError().text());
    }

    while (sql.next()) {
        const qint64 bookId = sql.value(0).toLongLong();
        const Result<QVector<float>> vector = decode(sql.value(1).toByteArray());
        if (vector.isErr())
            return vector.error();
        scored.append(qMakePair(bookId, cosine(query, vector.value())));
    }

    std::sort(scored.begin(), scored.end(),
              [](const QPair<qint64, float> &a, const QPair<qint64, float> &b) { return a.second > b.second; });
    if (scored.size() > limit)
        scored.resize(limit);

    return Result<QVector<QPair<qint64, float>>>::ok(scored);
}

Result<QPair<qint64, qint64>> VectorStore::chunkCoverage(qint64 bookId) const {
    qint64 total = 0;
    {
        QSqlQuery sql(m_db);
        sql.prepare(QStringLiteral("SELECT COUNT(*) FROM book_chunks WHERE book_id = ?"));
        sql.addBindValue(bookId);
        if (!sql.exec() || !sql.next())
            return Error::db(sql.lastError().text());
        total = sql.value(0).toLongLong();
    }

    qint64 embedded = 0;
    {
        QSqlQuery sql(m_db);
        sql.prepare(QStringLiteral("SELECT COUNT(*) FROM chunk_embeddings WHERE book_id = ?"));
        sql.addBindValue(bookId);
        if (!sql.exec() || !sql.next())
            return Error::db(sql.lastError().text());
        embedded = sql.value(0).toLongLong();
    }

    return Result<QPair<qint64, qint64>>::ok(qMakePair(embedded, total));
}

QString VectorStore::ftsTerms(const QString &query) {
    QStringList terms;
    const QStringList words = query.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    for (const QString &word : words) {
        QString cleaned;
        for (const QChar &c : word) {
            if (c.isLetterOrNumber())
                cleaned += c;
        }
        // Very short words are noise and match almost everything.
        if (cleaned.length() > 2)
            terms << QStringLiteral("\"%1\"").arg(cleaned);
    }
    return terms.join(QStringLiteral(" OR "));
}

float VectorStore::normaliseRank(double rank) {
    const double strength = std::max(-rank, 0.0);
    return static_cast<float>(strength / (strength + 4.0));
}
