// Small key/value settings, for things too minor to deserve a column.
// Ported from omabook-core/src/repo/settings.rs.
#pragma once

#include "core/result.h"

#include <QSqlDatabase>
#include <QString>
#include <optional>

// See BookRepository for why this takes a QSqlDatabase & rather than owning
// one.
class SettingsRepository {
public:
    explicit SettingsRepository(QSqlDatabase &db) : m_db(db) { }

    Result<std::optional<QString>> get(const QString &key) const;

    // `defaultValue` on a missing key, and on a failed read -- a settings
    // lookup that cannot fail the caller's flow, mirroring Rust's
    // get_or's `.ok().flatten()`.
    QString getOr(const QString &key, const QString &defaultValue) const;

    Result<void> set(const QString &key, const QString &value);

private:
    QSqlDatabase &m_db;
};
