#include "bookrepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <algorithm>

namespace {

// Every column build_book (below) expects, in order, plus the progress join.
const QString kSelectColumns = QStringLiteral(
    "b.id, b.title, b.subtitle, b.authors, b.source_path, b.reading_path, "
    "b.format, b.file_hash, b.file_size, b.cover_path, b.description, "
    "b.category_id, b.status, b.text_quality, b.added_at, b.is_favorite, "
    "COALESCE(p.fraction, 0.0) AS progress");

QString whereClause(const LibraryFilter &filter) {
    switch (filter.kind) {
    case LibraryFilter::Kind::All:
        return QStringLiteral("1 = 1");
    case LibraryFilter::Kind::Favorites:
        return QStringLiteral("b.is_favorite = 1");
    case LibraryFilter::Kind::Reading:
        return QStringLiteral("b.status = 'reading'");
    case LibraryFilter::Kind::Completed:
        return QStringLiteral("b.status = 'finished'");
    case LibraryFilter::Kind::Queue:
        return QStringLiteral("EXISTS (SELECT 1 FROM queue_items q "
                               "WHERE q.book_id = b.id AND q.removed_at IS NULL)");
    case LibraryFilter::Kind::Category:
        return QStringLiteral("b.category_id = :filterId");
    case LibraryFilter::Kind::Tag:
        return QStringLiteral("EXISTS (SELECT 1 FROM book_tags bt "
                               "WHERE bt.book_id = b.id AND bt.tag_id = :filterId)");
    }
    Q_UNREACHABLE_RETURN(QString());
}

bool filterHasParam(const LibraryFilter &filter) {
    return filter.kind == LibraryFilter::Kind::Category || filter.kind == LibraryFilter::Kind::Tag;
}

QString orderClause(BookSort sort) {
    switch (sort) {
    case BookSort::RecentlyAdded:
        return QStringLiteral("b.added_at DESC, b.id DESC");
    case BookSort::Title:
        return QStringLiteral("b.title COLLATE NOCASE ASC");
    case BookSort::Author:
        return QStringLiteral("b.authors COLLATE NOCASE ASC, b.title COLLATE NOCASE ASC");
    case BookSort::Progress:
        return QStringLiteral("progress DESC, b.title COLLATE NOCASE ASC");
    }
    Q_UNREACHABLE_RETURN(QString());
}

// The queue has its own hand-ordered sequence; any other sort would discard
// the point of it (SPEC §5.9). This overrides whatever BookSort was asked
// for.
QString effectiveOrder(const LibraryFilter &filter, BookSort sort) {
    if (filter.kind == LibraryFilter::Kind::Queue) {
        return QStringLiteral("(SELECT q.position FROM queue_items q "
                               "WHERE q.book_id = b.id AND q.removed_at IS NULL) ASC");
    }
    return orderClause(sort);
}

Result<Book> buildBook(const QSqlQuery &query) {
    Book book;
    book.id = query.value(0).toLongLong();
    book.title = query.value(1).toString();
    book.subtitle = query.value(2).toString();
    book.authors = Book::decodeAuthors(query.value(3).toString());
    book.sourcePath = query.value(4).toString();
    book.readingPath = query.value(5).toString();

    const Result<BookFormat> format = fromString<BookFormat>(query.value(6).toString());
    if (format.isErr())
        return format.error();
    book.format = format.value();

    book.fileHash = query.value(7).toString();
    book.fileSize = query.value(8).toLongLong();
    book.coverPath = query.value(9).toString();
    book.description = query.value(10).toString();
    book.categoryId = query.value(11).isNull() ? 0 : query.value(11).toLongLong();

    const Result<BookStatus> status = fromString<BookStatus>(query.value(12).toString());
    if (status.isErr())
        return status.error();
    book.status = status.value();

    const QVariant qualityValue = query.value(13);
    if (!qualityValue.isNull()) {
        const Result<TextQuality> quality = fromString<TextQuality>(qualityValue.toString());
        if (quality.isErr())
            return quality.error();
        book.textQuality = quality.value();
    }

    book.addedAt = query.value(14).toString();
    book.isFavorite = query.value(15).toLongLong() != 0;
    book.progress = query.value(16).toDouble();

    return Result<Book>::ok(book);
}

} // namespace

Result<QList<Book>> BookRepository::list(const LibraryFilter &filter, BookSort sort) const {
    const QString sql = QStringLiteral("SELECT %1 FROM books b "
                                        "LEFT JOIN reading_progress p ON p.book_id = b.id "
                                        "WHERE b.archived_at IS NULL AND (%2) "
                                        "ORDER BY %3")
                             .arg(kSelectColumns, whereClause(filter), effectiveOrder(filter, sort));

    QList<Book> books;
    {
        QSqlQuery query(m_db);
        if (!query.prepare(sql))
            return Error::db(query.lastError().text());
        if (filterHasParam(filter))
            query.bindValue(QStringLiteral(":filterId"), filter.id);
        if (!query.exec())
            return Error::db(query.lastError().text());

        while (query.next()) {
            const Result<Book> book = buildBook(query);
            if (book.isErr())
                return book.error();
            books.append(book.value());
        }
    }
    return Result<QList<Book>>::ok(books);
}

Result<QList<Book>> BookRepository::listIds(const QList<qint64> &ids) const {
    QList<Book> books;
    if (ids.isEmpty())
        return Result<QList<Book>>::ok(books);

    const QString sql = QStringLiteral("SELECT %1 FROM books b "
                                        "LEFT JOIN reading_progress p ON p.book_id = b.id "
                                        "WHERE b.id = :id")
                             .arg(kSelectColumns);

    {
        QSqlQuery query(m_db);
        if (!query.prepare(sql))
            return Error::db(query.lastError().text());

        for (qint64 id : ids) {
            query.bindValue(QStringLiteral(":id"), id);
            if (!query.exec())
                return Error::db(query.lastError().text());
            if (query.next()) {
                const Result<Book> book = buildBook(query);
                if (book.isErr())
                    return book.error();
                books.append(book.value());
            }
        }
    }
    return Result<QList<Book>>::ok(books);
}

QString BookRepository::ftsQuery(const QString &query) {
    const QStringList terms = query.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList clauses;
    for (const QString &term : terms) {
        QString cleaned;
        for (const QChar &c : term) {
            if (c.isLetterOrNumber() || c == QLatin1Char('\''))
                cleaned += c;
        }
        if (cleaned.isEmpty())
            continue;
        clauses << QStringLiteral("\"%1\"*").arg(cleaned);
    }
    return clauses.join(QLatin1Char(' '));
}

Result<QList<Book>> BookRepository::searchFts(const QString &query, BookSort sort) const {
    const QString sql = QStringLiteral("SELECT %1 FROM books_fts f "
                                        "JOIN books b ON b.id = f.rowid "
                                        "LEFT JOIN reading_progress p ON p.book_id = b.id "
                                        "WHERE books_fts MATCH :query AND b.archived_at IS NULL "
                                        "ORDER BY %2")
                             .arg(kSelectColumns, orderClause(sort));

    QList<Book> books;
    {
        QSqlQuery sqlQuery(m_db);
        if (!sqlQuery.prepare(sql))
            return Error::db(sqlQuery.lastError().text());
        sqlQuery.bindValue(QStringLiteral(":query"), ftsQuery(query));
        if (!sqlQuery.exec())
            return Error::db(sqlQuery.lastError().text());

        while (sqlQuery.next()) {
            const Result<Book> book = buildBook(sqlQuery);
            if (book.isErr())
                return book.error();
            books.append(book.value());
        }
    }
    return Result<QList<Book>>::ok(books);
}

Result<QList<Book>> BookRepository::searchSubstring(const QString &query, BookSort sort) const {
    const QString sql = QStringLiteral(
        "SELECT %1 FROM books b "
        "LEFT JOIN reading_progress p ON p.book_id = b.id "
        "WHERE b.archived_at IS NULL "
        "AND (b.title LIKE :pattern OR b.authors LIKE :pattern OR b.description LIKE :pattern) "
        "ORDER BY %2")
                             .arg(kSelectColumns, orderClause(sort));

    QList<Book> books;
    {
        QSqlQuery sqlQuery(m_db);
        if (!sqlQuery.prepare(sql))
            return Error::db(sqlQuery.lastError().text());
        sqlQuery.bindValue(QStringLiteral(":pattern"), QStringLiteral("%%1%").arg(query));
        if (!sqlQuery.exec())
            return Error::db(sqlQuery.lastError().text());

        while (sqlQuery.next()) {
            const Result<Book> book = buildBook(sqlQuery);
            if (book.isErr())
                return book.error();
            books.append(book.value());
        }
    }
    return Result<QList<Book>>::ok(books);
}

Result<SearchResult> BookRepository::search(const QString &query, BookSort sort) const {
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty())
        return Result<SearchResult>::ok(SearchResult{});

    const Result<QList<Book>> ftsResult = searchFts(trimmed, sort);
    if (ftsResult.isOk())
        return Result<SearchResult>::ok(SearchResult{ftsResult.value(), SearchStrategy::FullText});

    // Any FTS parse error -- a lone quote, a bare '*', an unbalanced
    // operator -- falls back to a substring match rather than surfacing: a
    // search box must never punish the user for typing punctuation.
    const Result<QList<Book>> substringResult = searchSubstring(trimmed, sort);
    if (substringResult.isErr())
        return substringResult.error();
    return Result<SearchResult>::ok(SearchResult{substringResult.value(), SearchStrategy::Substring});
}

Result<bool> BookRepository::isQueued(qint64 bookId) const {
    QSqlQuery query(m_db);
    if (!query.prepare(QStringLiteral(
            "SELECT EXISTS(SELECT 1 FROM queue_items WHERE book_id = :id AND removed_at IS NULL)")))
        return Error::db(query.lastError().text());
    query.bindValue(QStringLiteral(":id"), bookId);
    if (!query.exec() || !query.next())
        return Error::db(query.lastError().text());
    return Result<bool>::ok(query.value(0).toLongLong() != 0);
}

Result<bool> BookRepository::toggleQueued(qint64 bookId) {
    const Result<bool> queued = isQueued(bookId);
    if (queued.isErr())
        return queued.error();

    if (queued.value()) {
        QSqlQuery query(m_db);
        if (!query.prepare(QStringLiteral("UPDATE queue_items SET removed_at = :now, "
                                           "removal_reason = 'manual' "
                                           "WHERE book_id = :id AND removed_at IS NULL")))
            return Error::db(query.lastError().text());
        query.bindValue(QStringLiteral(":id"), bookId);
        query.bindValue(QStringLiteral(":now"), nowIso8601());
        if (!query.exec())
            return Error::db(query.lastError().text());
        return Result<bool>::ok(false);
    }

    // Gaps of 1 are fine; reordering rewrites positions wholesale.
    qint64 next = 1;
    {
        QSqlQuery query(m_db);
        if (!query.exec(QStringLiteral(
                "SELECT COALESCE(MAX(position), 0) + 1 FROM queue_items WHERE removed_at IS NULL")))
            return Error::db(query.lastError().text());
        if (query.next())
            next = query.value(0).toLongLong();
    }

    QSqlQuery insertQuery(m_db);
    if (!insertQuery.prepare(QStringLiteral(
            "INSERT INTO queue_items (book_id, position, added_at) VALUES (:id, :position, :now)")))
        return Error::db(insertQuery.lastError().text());
    insertQuery.bindValue(QStringLiteral(":id"), bookId);
    insertQuery.bindValue(QStringLiteral(":position"), next);
    insertQuery.bindValue(QStringLiteral(":now"), nowIso8601());
    if (!insertQuery.exec())
        return Error::db(insertQuery.lastError().text());
    return Result<bool>::ok(true);
}

Result<void> BookRepository::reorderQueue(const QList<qint64> &ordered) {
    QSqlQuery begin(m_db);
    // BEGIN IMMEDIATE, not QSqlDatabase::transaction(), so a deferred lock
    // upgrade cannot surprise us with SQLITE_BUSY partway through
    // (CLAUDE.md, "Database").
    if (!begin.exec(QStringLiteral("BEGIN IMMEDIATE")))
        return Error::db(begin.lastError().text());

    QSqlQuery update(m_db);
    if (!update.prepare(QStringLiteral(
            "UPDATE queue_items SET position = :position WHERE book_id = :id AND removed_at IS NULL"))) {
        QSqlQuery rollback(m_db);
        rollback.exec(QStringLiteral("ROLLBACK"));
        return Error::db(update.lastError().text());
    }

    for (int index = 0; index < ordered.size(); ++index) {
        update.bindValue(QStringLiteral(":id"), ordered.at(index));
        update.bindValue(QStringLiteral(":position"), static_cast<qint64>(index) + 1);
        if (!update.exec()) {
            const QString message = update.lastError().text();
            QSqlQuery rollback(m_db);
            rollback.exec(QStringLiteral("ROLLBACK"));
            return Error::db(message);
        }
    }

    QSqlQuery commit(m_db);
    if (!commit.exec(QStringLiteral("COMMIT")))
        return Error::db(commit.lastError().text());
    return VoidResult::ok();
}

Result<void> BookRepository::deleteBook(qint64 bookId) {
    QSqlQuery query(m_db);
    if (!query.prepare(QStringLiteral("DELETE FROM books WHERE id = :id")))
        return Error::db(query.lastError().text());
    query.bindValue(QStringLiteral(":id"), bookId);
    if (!query.exec())
        return Error::db(query.lastError().text());
    return VoidResult::ok();
}

Result<bool> BookRepository::toggleFavorite(qint64 bookId) {
    {
        QSqlQuery update(m_db);
        if (!update.prepare(QStringLiteral("UPDATE books SET is_favorite = "
                                            "CASE is_favorite WHEN 1 THEN 0 ELSE 1 END WHERE id = :id")))
            return Error::db(update.lastError().text());
        update.bindValue(QStringLiteral(":id"), bookId);
        if (!update.exec())
            return Error::db(update.lastError().text());
    }

    QSqlQuery query(m_db);
    if (!query.prepare(QStringLiteral("SELECT is_favorite FROM books WHERE id = :id")))
        return Error::db(query.lastError().text());
    query.bindValue(QStringLiteral(":id"), bookId);
    if (!query.exec() || !query.next())
        return Error::db(query.lastError().text());
    return Result<bool>::ok(query.value(0).toLongLong() != 0);
}

Result<FilterCounts> BookRepository::counts() const {
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT "
                                    "COUNT(*), "
                                    "COALESCE(SUM(is_favorite = 1), 0), "
                                    "COALESCE(SUM(status = 'reading'), 0), "
                                    "COALESCE(SUM(status = 'finished'), 0), "
                                    "(SELECT COUNT(*) FROM queue_items WHERE removed_at IS NULL) "
                                    "FROM books WHERE archived_at IS NULL")))
        return Error::db(query.lastError().text());
    if (!query.next())
        return Error::db(QStringLiteral("counts query returned no row"));

    FilterCounts result;
    result.all = query.value(0).toLongLong();
    result.favorites = query.value(1).toLongLong();
    result.reading = query.value(2).toLongLong();
    result.completed = query.value(3).toLongLong();
    result.queue = query.value(4).toLongLong();
    return Result<FilterCounts>::ok(result);
}

Result<std::optional<Book>> BookRepository::find(qint64 id) const {
    const QString sql = QStringLiteral("SELECT %1 FROM books b "
                                        "LEFT JOIN reading_progress p ON p.book_id = b.id "
                                        "WHERE b.id = :id")
                             .arg(kSelectColumns);
    QSqlQuery query(m_db);
    if (!query.prepare(sql))
        return Error::db(query.lastError().text());
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec())
        return Error::db(query.lastError().text());
    if (!query.next())
        return Result<std::optional<Book>>::ok(std::nullopt);

    const Result<Book> book = buildBook(query);
    if (book.isErr())
        return book.error();
    return Result<std::optional<Book>>::ok(book.value());
}

Result<std::optional<Book>> BookRepository::findByHash(const QString &fileHash) const {
    const QString sql = QStringLiteral("SELECT %1 FROM books b "
                                        "LEFT JOIN reading_progress p ON p.book_id = b.id "
                                        "WHERE b.file_hash = :fileHash")
                             .arg(kSelectColumns);
    QSqlQuery query(m_db);
    if (!query.prepare(sql))
        return Error::db(query.lastError().text());
    query.bindValue(QStringLiteral(":fileHash"), fileHash);
    if (!query.exec())
        return Error::db(query.lastError().text());
    if (!query.next())
        return Result<std::optional<Book>>::ok(std::nullopt);

    const Result<Book> book = buildBook(query);
    if (book.isErr())
        return book.error();
    return Result<std::optional<Book>>::ok(book.value());
}

Result<qint64> BookRepository::count() const {
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM books WHERE archived_at IS NULL")))
        return Error::db(query.lastError().text());
    if (!query.next())
        return Error::db(QStringLiteral("count query returned no row"));
    return Result<qint64>::ok(query.value(0).toLongLong());
}

Result<qint64> BookRepository::insert(const NewBook &book) {
    const Result<std::optional<Book>> existing = findByHash(book.fileHash);
    if (existing.isErr())
        return existing.error();
    if (existing.value().has_value())
        return Result<qint64>::ok(existing.value()->id);

    const QString now = nowIso8601();
    QSqlQuery query(m_db);
    if (!query.prepare(QStringLiteral(
            "INSERT INTO books "
            "(title, authors, source_path, format, file_hash, file_size, added_at, "
            " description, publisher, language, published_date, isbn13, "
            " cover_path, text_quality, page_count, metadata_fetched_at) "
            "VALUES (:title, :authors, :sourcePath, :format, :fileHash, :fileSize, :addedAt, "
            "        :description, :publisher, :language, :publishedDate, :isbn13, "
            "        :coverPath, :textQuality, :pageCount, :addedAt)")))
        return Error::db(query.lastError().text());

    query.bindValue(QStringLiteral(":title"), book.title);
    query.bindValue(QStringLiteral(":authors"), Book::encodeAuthors(book.authors));
    query.bindValue(QStringLiteral(":sourcePath"), book.sourcePath);
    query.bindValue(QStringLiteral(":format"), toString(book.format));
    query.bindValue(QStringLiteral(":fileHash"), book.fileHash);
    query.bindValue(QStringLiteral(":fileSize"), book.fileSize);
    query.bindValue(QStringLiteral(":addedAt"), now);
    query.bindValue(QStringLiteral(":description"), book.description);
    query.bindValue(QStringLiteral(":publisher"), book.publisher);
    query.bindValue(QStringLiteral(":language"), book.language);
    query.bindValue(QStringLiteral(":publishedDate"), book.publishedDate);
    query.bindValue(QStringLiteral(":isbn13"), book.isbn13);
    query.bindValue(QStringLiteral(":coverPath"), book.coverPath);
    if (book.textQuality.has_value())
        query.bindValue(QStringLiteral(":textQuality"), toString(*book.textQuality));
    else
        query.bindValue(QStringLiteral(":textQuality"), QVariant());
    if (book.pageCount.has_value())
        query.bindValue(QStringLiteral(":pageCount"), *book.pageCount);
    else
        query.bindValue(QStringLiteral(":pageCount"), QVariant());

    if (!query.exec())
        return Error::db(query.lastError().text());

    return Result<qint64>::ok(query.lastInsertId().toLongLong());
}

Result<void> BookRepository::saveProgress(qint64 bookId, const QString &position, double fraction) {
    const double clamped = std::clamp(fraction, 0.0, 1.0);
    const QString now = nowIso8601();

    {
        QSqlQuery query(m_db);
        if (!query.prepare(QStringLiteral(
                "INSERT INTO reading_progress (book_id, position, fraction, updated_at) "
                "VALUES (:id, :position, :fraction, :now) "
                "ON CONFLICT(book_id) DO UPDATE SET "
                "position = excluded.position, fraction = excluded.fraction, "
                "updated_at = excluded.updated_at")))
            return Error::db(query.lastError().text());
        query.bindValue(QStringLiteral(":id"), bookId);
        query.bindValue(QStringLiteral(":position"), position);
        query.bindValue(QStringLiteral(":fraction"), clamped);
        query.bindValue(QStringLiteral(":now"), now);
        if (!query.exec())
            return Error::db(query.lastError().text());
    }

    {
        QSqlQuery query(m_db);
        if (!query.prepare(QStringLiteral("UPDATE books SET status = 'reading', "
                                           "started_at = COALESCE(started_at, :now) "
                                           "WHERE id = :id AND status = 'unread'")))
            return Error::db(query.lastError().text());
        query.bindValue(QStringLiteral(":id"), bookId);
        query.bindValue(QStringLiteral(":now"), now);
        if (!query.exec())
            return Error::db(query.lastError().text());
    }

    if (clamped >= COMPLETION_THRESHOLD) {
        QSqlQuery query(m_db);
        if (!query.prepare(QStringLiteral("UPDATE books SET status = 'finished', "
                                           "completed_at = COALESCE(completed_at, :now) "
                                           "WHERE id = :id AND status != 'finished'")))
            return Error::db(query.lastError().text());
        query.bindValue(QStringLiteral(":id"), bookId);
        query.bindValue(QStringLiteral(":now"), now);
        if (!query.exec())
            return Error::db(query.lastError().text());
    }

    return VoidResult::ok();
}

Result<void> BookRepository::setTextQuality(qint64 bookId, TextQuality quality) {
    QSqlQuery query(m_db);
    if (!query.prepare(QStringLiteral("UPDATE books SET text_quality = :quality WHERE id = :id")))
        return Error::db(query.lastError().text());
    query.bindValue(QStringLiteral(":id"), bookId);
    query.bindValue(QStringLiteral(":quality"), toString(quality));
    if (!query.exec())
        return Error::db(query.lastError().text());
    return VoidResult::ok();
}

Result<std::optional<QString>> BookRepository::progressPosition(qint64 bookId) const {
    QSqlQuery query(m_db);
    if (!query.prepare(QStringLiteral("SELECT position FROM reading_progress WHERE book_id = :id")))
        return Error::db(query.lastError().text());
    query.bindValue(QStringLiteral(":id"), bookId);
    if (!query.exec())
        return Error::db(query.lastError().text());
    if (!query.next())
        return Result<std::optional<QString>>::ok(std::nullopt);

    const QVariant value = query.value(0);
    if (value.isNull())
        return Result<std::optional<QString>>::ok(std::nullopt);
    return Result<std::optional<QString>>::ok(value.toString());
}
