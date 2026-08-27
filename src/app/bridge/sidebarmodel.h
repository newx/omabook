// SidebarModel -- the fixed views, plus the collapsible Categories and Tags
// lists (SPEC §5.1). Ported from omabook-app/src/bridge/sidebar.rs.
//
// Kept separate from LibraryModel so each has one reason to change: this
// object answers "what can I filter by", the other "what am I looking at".
//
// The taxonomy lists are handed to QML as JSON rather than as two more
// QAbstractListModels: they are small, read-only, and rebuilt wholesale on
// every reload, so a list model's incremental-update machinery would be
// pure ceremony. The book grid, which is large and needs real row
// semantics, uses a proper model instead (LibraryModel).
#pragma once

#include <QObject>
#include <QString>

class SidebarModel : public QObject {
    Q_OBJECT
    // Snake case, and deliberately: the QML reads `sidebarData.count_all`
    // and `sidebarData.categories_json`, because cxx-qt never camelCased a
    // multi-word qproperty and the ported QML carries those names
    // byte-for-byte (CLAUDE.md, "QML conventions"). The C++ accessors stay
    // camelCase; only the QML-visible token differs.
    Q_PROPERTY(int count_all READ countAll NOTIFY countAllChanged)
    Q_PROPERTY(int count_favorites READ countFavorites NOTIFY countFavoritesChanged)
    Q_PROPERTY(int count_reading READ countReading NOTIFY countReadingChanged)
    Q_PROPERTY(int count_queue READ countQueue NOTIFY countQueueChanged)
    Q_PROPERTY(int count_completed READ countCompleted NOTIFY countCompletedChanged)
    Q_PROPERTY(QString categories_json READ categoriesJson NOTIFY categoriesJsonChanged)
    Q_PROPERTY(QString tags_json READ tagsJson NOTIFY tagsJsonChanged)

public:
    explicit SidebarModel(QObject *parent = nullptr);

    int countAll() const { return m_countAll; }
    int countFavorites() const { return m_countFavorites; }
    int countReading() const { return m_countReading; }
    int countQueue() const { return m_countQueue; }
    int countCompleted() const { return m_countCompleted; }
    QString categoriesJson() const { return m_categoriesJson; }
    QString tagsJson() const { return m_tagsJson; }

    // Re-read the counts, categories and tags from the database.
    Q_INVOKABLE void reload();

signals:
    void countAllChanged();
    void countFavoritesChanged();
    void countReadingChanged();
    void countQueueChanged();
    void countCompletedChanged();
    void categoriesJsonChanged();
    void tagsJsonChanged();

private:
    void setCountAll(int count);
    void setCountFavorites(int count);
    void setCountReading(int count);
    void setCountQueue(int count);
    void setCountCompleted(int count);
    void setCategoriesJson(const QString &json);
    void setTagsJson(const QString &json);

    int m_countAll = 0;
    int m_countFavorites = 0;
    int m_countReading = 0;
    int m_countQueue = 0;
    int m_countCompleted = 0;
    QString m_categoriesJson = QStringLiteral("[]");
    QString m_tagsJson = QStringLiteral("[]");
};
