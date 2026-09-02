// Categories (a tree, one per book) and tags (flat, many per book).
#pragma once

#include "core/result.h"

#include <QList>
#include <QSqlDatabase>
#include <QString>

// A sidebar entry: something to filter by, and how many books it holds.
struct TaxonomyEntry {
    qint64 id = 0;
    QString name;
    qint64 bookCount = 0;
};

// URL- and comparison-safe form of a name. Also the uniqueness key, so
// "Sci-Fi" and "sci fi" cannot both be created. Static so it is testable
// without a database (CLAUDE.md, "Pure logic goes in static member
// functions").
QString slugify(const QString &name);

// See BookRepository for why this takes a QSqlDatabase & rather than owning
// one.
class CategoryRepository {
public:
    explicit CategoryRepository(QSqlDatabase &db) : m_db(db) { }

    // Find or create by slug, returning the id.
    Result<qint64> ensure(const QString &name);

    // Categories that actually hold a non-archived book, with counts,
    // alphabetically.
    Result<QList<TaxonomyEntry>> listWithCounts() const;

    Result<void> assign(qint64 bookId, qint64 categoryId);

private:
    QSqlDatabase &m_db;
};

class TagRepository {
public:
    explicit TagRepository(QSqlDatabase &db) : m_db(db) { }

    Result<qint64> ensure(const QString &name);

    // Attach a tag to a book. Re-tagging is a no-op rather than an error.
    Result<void> attach(qint64 bookId, qint64 tagId, const QString &source);

    // Tags that actually hold a non-archived book, by count descending then
    // name.
    Result<QList<TaxonomyEntry>> listWithCounts() const;

private:
    QSqlDatabase &m_db;
};
