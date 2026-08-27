#include "sidebarmodel.h"

#include "core/db/database.h"
#include "core/repo/bookrepository.h"
#include "core/repo/taxonomy.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

// [{"id":N,"name":"...","count":N}]. Built with QJsonDocument rather than
// string concatenation -- the Rust build hand-rolled an escaper for this,
// which QJsonDocument makes unnecessary.
QString toJson(const QList<TaxonomyEntry> &entries) {
    QJsonArray items;
    for (const TaxonomyEntry &entry : entries) {
        QJsonObject item;
        item.insert(QStringLiteral("id"), entry.id);
        item.insert(QStringLiteral("name"), entry.name);
        item.insert(QStringLiteral("count"), entry.bookCount);
        items.append(item);
    }
    return QString::fromUtf8(QJsonDocument(items).toJson(QJsonDocument::Compact));
}

} // namespace

SidebarModel::SidebarModel(QObject *parent) : QObject(parent) { }

void SidebarModel::reload() {
    QSqlDatabase &db = Database::forCurrentThread().connection();

    const Result<FilterCounts> counts = BookRepository(db).counts();
    if (counts.isErr()) {
        qWarning("could not read library counts: %s", qUtf8Printable(counts.error().message));
        return;
    }

    // Categories and tags default to empty on their own failure rather than
    // aborting the whole reload -- the fixed-view counts above are the more
    // important half of the sidebar.
    const Result<QList<TaxonomyEntry>> categories = CategoryRepository(db).listWithCounts();
    const Result<QList<TaxonomyEntry>> tags = TagRepository(db).listWithCounts();

    setCountAll(static_cast<int>(counts.value().all));
    setCountFavorites(static_cast<int>(counts.value().favorites));
    setCountReading(static_cast<int>(counts.value().reading));
    setCountQueue(static_cast<int>(counts.value().queue));
    setCountCompleted(static_cast<int>(counts.value().completed));
    setCategoriesJson(categories.isOk() ? toJson(categories.value()) : QStringLiteral("[]"));
    setTagsJson(tags.isOk() ? toJson(tags.value()) : QStringLiteral("[]"));
}

void SidebarModel::setCountAll(int count) {
    if (m_countAll == count)
        return;

    m_countAll = count;
    emit countAllChanged();
}

void SidebarModel::setCountFavorites(int count) {
    if (m_countFavorites == count)
        return;

    m_countFavorites = count;
    emit countFavoritesChanged();
}

void SidebarModel::setCountReading(int count) {
    if (m_countReading == count)
        return;

    m_countReading = count;
    emit countReadingChanged();
}

void SidebarModel::setCountQueue(int count) {
    if (m_countQueue == count)
        return;

    m_countQueue = count;
    emit countQueueChanged();
}

void SidebarModel::setCountCompleted(int count) {
    if (m_countCompleted == count)
        return;

    m_countCompleted = count;
    emit countCompletedChanged();
}

void SidebarModel::setCategoriesJson(const QString &json) {
    if (m_categoriesJson == json)
        return;

    m_categoriesJson = json;
    emit categoriesJsonChanged();
}

void SidebarModel::setTagsJson(const QString &json) {
    if (m_tagsJson == json)
        return;

    m_tagsJson = json;
    emit tagsJsonChanged();
}
