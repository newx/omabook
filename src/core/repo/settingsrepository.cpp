#include "settingsrepository.h"

#include <QSqlError>
#include <QSqlQuery>

Result<std::optional<QString>> SettingsRepository::get(const QString &key) const {
    QSqlQuery query(m_db);
    if (!query.prepare(QStringLiteral("SELECT value FROM settings WHERE key = :key")))
        return Error::db(query.lastError().text());
    query.bindValue(QStringLiteral(":key"), key);
    if (!query.exec())
        return Error::db(query.lastError().text());
    if (!query.next())
        return Result<std::optional<QString>>::ok(std::nullopt);
    return Result<std::optional<QString>>::ok(query.value(0).toString());
}

QString SettingsRepository::getOr(const QString &key, const QString &defaultValue) const {
    const Result<std::optional<QString>> result = get(key);
    if (result.isErr() || !result.value().has_value())
        return defaultValue;
    return *result.value();
}

Result<void> SettingsRepository::set(const QString &key, const QString &value) {
    QSqlQuery query(m_db);
    if (!query.prepare(QStringLiteral("INSERT INTO settings (key, value) VALUES (:key, :value) "
                                       "ON CONFLICT(key) DO UPDATE SET value = excluded.value")))
        return Error::db(query.lastError().text());
    query.bindValue(QStringLiteral(":key"), key);
    query.bindValue(QStringLiteral(":value"), value);
    if (!query.exec())
        return Error::db(query.lastError().text());
    return VoidResult::ok();
}
