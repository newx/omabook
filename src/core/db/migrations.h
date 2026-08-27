// Schema migrations, applied in order and tracked with `PRAGMA user_version`.
// Ported from omabook-core/src/db/migrations.rs.
//
// Adding a migration means appending a new `.sql` file under
// src/core/db/migrations/, registering it in migrations.qrc, and adding one
// line to the kMigrations table in migrations.cpp; never edit a shipped one,
// since a user's database has already run it.
#pragma once

#include "core/result.h"

#include <QSqlDatabase>
#include <QString>
#include <QStringList>

// The version a fully-migrated database reports. Derived from the number of
// registered migrations (see migrations.cpp) rather than written as a
// literal here too, so the two can never drift apart.
extern const qint64 SCHEMA_VERSION;

// Reads `PRAGMA user_version` from `db` and applies every migration beyond
// it, each inside its own transaction, bumping `user_version` as it goes.
// Refuses -- without touching the database -- if the reported version is
// already newer than SCHEMA_VERSION, which is downgrade protection: running
// an older build against a newer database would silently misinterpret its
// schema.
Result<void> migrate(QSqlDatabase &db);

// Splits one migration file's SQL text into individual statements, so that
// QSqlQuery::exec() -- which runs exactly one statement -- can execute them
// one at a time. Tracks BEGIN/END nesting so a `CREATE TRIGGER ... BEGIN
// ... ; ... ; END;` body comes back as a single statement rather than being
// cut at its internal semicolons, and skips semicolons inside string
// literals, quoted identifiers, and `--`/`/* */` comments. Declared at
// namespace scope rather than as a class member -- there is no natural
// class to hang it on here -- specifically so tests can call it directly,
// the same testability CLAUDE.md asks of a static member function.
QStringList splitStatements(const QString &sql);
