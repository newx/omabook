#include "core/ai/indexer.h"

#include "core/ai/vectors.h"
#include "core/import/epub.h"
#include "core/import/pdf.h"
#include "core/repo/bookrepository.h"

#include <QCryptographicHash>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>

namespace {

// PDF text comes from poppler over the whole page range; every other format
// (after import, readablePath() always points at EPUB or PDF -- see
// models/book.h) is read through the EPUB spine walker. A missing or
// unreadable source degrades to empty text rather than an error: an
// unindexable book is a normal outcome, not a failure of chunking.
QString extractFullText(const Book &book) {
    if (book.format == BookFormat::Pdf) {
        const std::optional<int> pages = Pdf::pageCount(book.readablePath());
        if (!pages || *pages <= 0)
            return QString();
        return Pdf::extractText(book.readablePath(), 1, *pages).value_or(QString());
    }

    const Result<QString> text = Epub::extractText(book.readablePath());
    return text.isOk() ? text.value() : QString();
}

} // namespace

namespace Indexer {

QStringList split(const QString &text) {
    QStringList chunks;
    QString current;

    const QStringList paragraphs = text.split(QStringLiteral("\n\n"));
    for (const QString &raw : paragraphs) {
        // Collapse each paragraph's internal whitespace to single spaces --
        // wrapped source lines should not become line breaks in a chunk.
        const QString paragraph =
                raw.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).join(QLatin1Char(' '));
        if (paragraph.isEmpty())
            continue;

        // Flush the chunk in progress before appending, but only once it
        // already carries enough context to stand on its own -- otherwise a
        // string of short paragraphs would each become their own chunk.
        if (current.size() + paragraph.size() > TARGET_CHUNK_CHARS && current.size() >= MIN_CHUNK_CHARS) {
            chunks.append(current);
            current.clear();
        }

        if (!current.isEmpty())
            current.append(QLatin1Char(' '));
        current.append(paragraph);

        // A single paragraph longer than the target is split on its own,
        // hard at the character boundary, repeatedly until it is back under
        // the ceiling.
        while (current.size() > TARGET_CHUNK_CHARS * 2) {
            chunks.append(current.left(TARGET_CHUNK_CHARS));
            current = current.mid(TARGET_CHUNK_CHARS);
        }
    }

    if (current.size() >= MIN_CHUNK_CHARS) {
        chunks.append(current);
    } else if (!chunks.isEmpty()) {
        // A short tail belongs with the chunk before it, not on its own.
        chunks.last().append(QLatin1Char(' ')).append(current);
    } else if (!current.trimmed().isEmpty()) {
        // The whole document was shorter than MIN_CHUNK_CHARS -- still one
        // chunk, not nothing.
        chunks.append(current);
    }

    return chunks;
}

Result<int> chunkBook(QSqlDatabase &db, qint64 bookId) {
    const BookRepository repo(db);
    const Result<std::optional<Book>> found = repo.find(bookId);
    if (found.isErr())
        return found.error();
    if (!found.value())
        return Result<int>::ok(0);
    const Book &book = *found.value();

    int existing = 0;
    {
        QSqlQuery count(db);
        count.prepare(QStringLiteral("SELECT COUNT(*) FROM book_chunks WHERE book_id = ?"));
        count.addBindValue(bookId);
        if (!count.exec() || !count.next())
            return Error::db(count.lastError().text());
        existing = count.value(0).toInt();
    }
    // Idempotent: a book already chunked is left alone rather than
    // re-split, so re-running indexing over a library is always cheap.
    if (existing > 0)
        return Result<int>::ok(existing);

    const QString text = extractFullText(book);
    if (text.trimmed().isEmpty()) {
        qInfo() << "book" << bookId << "has no extractable text; nothing to index";
        return Result<int>::ok(0);
    }

    const QStringList chunks = split(text);
    const QString now = nowIso8601();

    QSqlQuery begin(db);
    // BEGIN IMMEDIATE, not QSqlDatabase::transaction(), so a deferred lock
    // upgrade cannot surprise us with SQLITE_BUSY partway through
    // (CLAUDE.md, "Database").
    if (!begin.exec(QStringLiteral("BEGIN IMMEDIATE")))
        return Error::db(begin.lastError().text());

    QSqlQuery insert(db);
    if (!insert.prepare(QStringLiteral(
                "INSERT INTO book_chunks (book_id, ordinal, text) VALUES (:book, :ordinal, :text)"))) {
        QSqlQuery rollback(db);
        rollback.exec(QStringLiteral("ROLLBACK"));
        return Error::db(insert.lastError().text());
    }
    for (int i = 0; i < chunks.size(); ++i) {
        insert.bindValue(QStringLiteral(":book"), bookId);
        insert.bindValue(QStringLiteral(":ordinal"), i);
        insert.bindValue(QStringLiteral(":text"), chunks.at(i));
        if (!insert.exec()) {
            const QString message = insert.lastError().text();
            QSqlQuery rollback(db);
            rollback.exec(QStringLiteral("ROLLBACK"));
            return Error::db(message);
        }
    }

    QSqlQuery stamp(db);
    if (!stamp.prepare(QStringLiteral("UPDATE books SET chunked_at = :now WHERE id = :id"))) {
        QSqlQuery rollback(db);
        rollback.exec(QStringLiteral("ROLLBACK"));
        return Error::db(stamp.lastError().text());
    }
    stamp.bindValue(QStringLiteral(":now"), now);
    stamp.bindValue(QStringLiteral(":id"), bookId);
    if (!stamp.exec()) {
        const QString message = stamp.lastError().text();
        QSqlQuery rollback(db);
        rollback.exec(QStringLiteral("ROLLBACK"));
        return Error::db(message);
    }

    QSqlQuery commit(db);
    if (!commit.exec(QStringLiteral("COMMIT")))
        return Error::db(commit.lastError().text());

    return Result<int>::ok(chunks.size());
}

Result<IndexReport> embedBook(QSqlDatabase &db, qint64 bookId, EmbedProvider &embedder,
                               std::function<void(int, int)> progress, std::function<bool()> shouldContinue) {
    VectorStore store(db);
    const QString model = embedder.model();

    QVector<QPair<qint64, QString>> pending;
    {
        // LEFT JOIN against chunk_embeddings and keep only the unmatched
        // rows: this is what makes a run resumable, since chunks already
        // embedded simply never appear here again.
        QSqlQuery sql(db);
        sql.prepare(QStringLiteral("SELECT c.id, c.text "
                                    "  FROM book_chunks c "
                                    "  LEFT JOIN chunk_embeddings e ON e.chunk_id = c.id "
                                    " WHERE c.book_id = ? AND e.chunk_id IS NULL "
                                    " ORDER BY c.ordinal"));
        sql.addBindValue(bookId);
        if (!sql.exec())
            return Error::db(sql.lastError().text());
        while (sql.next())
            pending.append(qMakePair(sql.value(0).toLongLong(), sql.value(1).toString()));
    }

    const Result<QPair<qint64, qint64>> coverage = store.chunkCoverage(bookId);
    if (coverage.isErr())
        return coverage.error();

    IndexReport report;
    report.alreadyDone = static_cast<int>(coverage.value().first);

    for (int i = 0; i < pending.size(); ++i) {
        // Checked before every chunk, not just at the start, so a cancel or
        // a power unplug during a long book stops promptly with whatever
        // was embedded so far kept.
        if (!shouldContinue()) {
            qInfo() << "indexing book" << bookId << "stopped part-way; progress is kept";
            break;
        }
        progress(i, pending.size());

        const qint64 chunkId = pending.at(i).first;
        const QString &text = pending.at(i).second;

        const Result<QVector<float>> vector = embedder.embed(text);
        if (vector.isErr()) {
            // One bad chunk should not abandon the book.
            qWarning() << "could not embed chunk" << chunkId << "of book" << bookId << ":"
                       << vector.error().message;
            continue;
        }

        const Result<void> put = store.putChunk(chunkId, bookId, model, vector.value());
        if (put.isErr())
            return put.error();
        ++report.chunksEmbedded;
    }

    return Result<IndexReport>::ok(report);
}

Result<bool> embedBookMetadata(QSqlDatabase &db, qint64 bookId, EmbedProvider &embedder) {
    const BookRepository repo(db);
    const Result<std::optional<Book>> found = repo.find(bookId);
    if (found.isErr())
        return found.error();
    if (!found.value())
        return Result<bool>::ok(false);
    const Book &book = *found.value();

    const Result<QString> source = metadataText(db, book);
    if (source.isErr())
        return source.error();
    const QString hash = shortHash(source.value());

    {
        // Skip when the same metadata was already embedded, so re-running
        // metadata indexing over a whole library is cheap once it has been
        // done.
        QSqlQuery sql(db);
        sql.prepare(QStringLiteral("SELECT source_hash FROM book_embeddings WHERE book_id = ?"));
        sql.addBindValue(bookId);
        if (!sql.exec())
            return Error::db(sql.lastError().text());
        if (sql.next() && sql.value(0).toString() == hash)
            return Result<bool>::ok(false);
    }

    const Result<QVector<float>> vector = embedder.embed(source.value());
    if (vector.isErr())
        return vector.error();

    VectorStore store(db);
    const Result<void> put = store.putBook(bookId, embedder.model(), hash, vector.value());
    if (put.isErr())
        return put.error();

    return Result<bool>::ok(true);
}

Result<QString> metadataText(QSqlDatabase &db, const Book &book) {
    QStringList parts;
    parts << book.title;
    if (!book.authors.isEmpty())
        parts << QStringLiteral("by %1").arg(book.authors.join(QStringLiteral(", ")));
    // Descriptions can run to pages; the opening carries the subject.
    if (!book.description.trimmed().isEmpty())
        parts << book.description.left(600);

    {
        QSqlQuery sql(db);
        sql.prepare(QStringLiteral(
                "SELECT c.name FROM categories c JOIN books b ON b.category_id = c.id WHERE b.id = ?"));
        sql.addBindValue(book.id);
        if (!sql.exec())
            return Error::db(sql.lastError().text());
        if (sql.next())
            parts << sql.value(0).toString();
    }

    {
        QSqlQuery sql(db);
        sql.prepare(QStringLiteral(
                "SELECT t.name FROM tags t JOIN book_tags bt ON bt.tag_id = t.id WHERE bt.book_id = ?"));
        sql.addBindValue(book.id);
        if (!sql.exec())
            return Error::db(sql.lastError().text());
        while (sql.next())
            parts << sql.value(0).toString();
    }

    return Result<QString>::ok(parts.join(QStringLiteral(". ")));
}

QString shortHash(const QString &text) {
    const QByteArray digest = QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toHex()).left(16);
}

} // namespace Indexer
