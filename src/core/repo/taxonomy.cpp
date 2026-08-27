#include "taxonomy.h"

#include <QSqlError>
#include <QSqlQuery>

namespace {

Result<QList<TaxonomyEntry>> runListWithCounts(QSqlDatabase &db, const QString &sql) {
    QSqlQuery query(db);
    if (!query.exec(sql))
        return Error::db(query.lastError().text());

    QList<TaxonomyEntry> entries;
    while (query.next()) {
        TaxonomyEntry entry;
        entry.id = query.value(0).toLongLong();
        entry.name = query.value(1).toString();
        entry.bookCount = query.value(2).toLongLong();
        entries.append(entry);
    }
    return Result<QList<TaxonomyEntry>>::ok(entries);
}

// Find-or-create-by-slug is identical for categories and tags; only the
// table name differs.
Result<qint64> ensureInTable(QSqlDatabase &db, const QString &table, const QString &name) {
    const QString slug = slugify(name);

    {
        QSqlQuery query(db);
        if (!query.prepare(QStringLiteral("SELECT id FROM %1 WHERE slug = :slug").arg(table)))
            return Error::db(query.lastError().text());
        query.bindValue(QStringLiteral(":slug"), slug);
        if (!query.exec())
            return Error::db(query.lastError().text());
        if (query.next())
            return Result<qint64>::ok(query.value(0).toLongLong());
    }

    QSqlQuery insert(db);
    if (!insert.prepare(
            QStringLiteral("INSERT INTO %1 (name, slug) VALUES (:name, :slug)").arg(table)))
        return Error::db(insert.lastError().text());
    insert.bindValue(QStringLiteral(":name"), name.trimmed());
    insert.bindValue(QStringLiteral(":slug"), slug);
    if (!insert.exec())
        return Error::db(insert.lastError().text());
    return Result<qint64>::ok(insert.lastInsertId().toLongLong());
}

} // namespace

QString slugify(const QString &name) {
    QString slug;
    slug.reserve(name.size());
    bool lastDash = true; // leading dashes are suppressed

    for (const QChar &ch : name) {
        if (ch.isLetterOrNumber()) {
            slug += ch.toLower();
            lastDash = false;
        } else if (!lastDash) {
            slug += QLatin1Char('-');
            lastDash = true;
        }
    }

    while (slug.endsWith(QLatin1Char('-')))
        slug.chop(1);

    return slug;
}

Result<qint64> CategoryRepository::ensure(const QString &name) {
    return ensureInTable(m_db, QStringLiteral("categories"), name);
}

Result<QList<TaxonomyEntry>> CategoryRepository::listWithCounts() const {
    return runListWithCounts(m_db,
                              QStringLiteral("SELECT c.id, c.name, COUNT(b.id) AS n "
                                             "FROM categories c "
                                             "JOIN books b ON b.category_id = c.id AND b.archived_at IS NULL "
                                             "GROUP BY c.id, c.name "
                                             "HAVING n > 0 "
                                             "ORDER BY c.name COLLATE NOCASE"));
}

Result<void> CategoryRepository::assign(qint64 bookId, qint64 categoryId) {
    QSqlQuery query(m_db);
    if (!query.prepare(QStringLiteral("UPDATE books SET category_id = :categoryId WHERE id = :bookId")))
        return Error::db(query.lastError().text());
    query.bindValue(QStringLiteral(":bookId"), bookId);
    query.bindValue(QStringLiteral(":categoryId"), categoryId);
    if (!query.exec())
        return Error::db(query.lastError().text());
    return VoidResult::ok();
}

Result<qint64> TagRepository::ensure(const QString &name) {
    return ensureInTable(m_db, QStringLiteral("tags"), name);
}

Result<void> TagRepository::attach(qint64 bookId, qint64 tagId, const QString &source) {
    QSqlQuery query(m_db);
    if (!query.prepare(QStringLiteral("INSERT INTO book_tags (book_id, tag_id, source) "
                                       "VALUES (:bookId, :tagId, :source) "
                                       "ON CONFLICT(book_id, tag_id) DO NOTHING")))
        return Error::db(query.lastError().text());
    query.bindValue(QStringLiteral(":bookId"), bookId);
    query.bindValue(QStringLiteral(":tagId"), tagId);
    query.bindValue(QStringLiteral(":source"), source);
    if (!query.exec())
        return Error::db(query.lastError().text());
    return VoidResult::ok();
}

Result<QList<TaxonomyEntry>> TagRepository::listWithCounts() const {
    return runListWithCounts(m_db,
                              QStringLiteral("SELECT t.id, t.name, COUNT(bt.book_id) AS n "
                                             "FROM tags t "
                                             "JOIN book_tags bt ON bt.tag_id = t.id "
                                             "JOIN books b ON b.id = bt.book_id AND b.archived_at IS NULL "
                                             "GROUP BY t.id, t.name "
                                             "HAVING n > 0 "
                                             "ORDER BY n DESC, t.name COLLATE NOCASE"));
}
