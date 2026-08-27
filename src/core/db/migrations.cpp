#include "migrations.h"

#include <QFile>
#include <QSqlError>
#include <QSqlQuery>

namespace {

// Each entry is applied once, in order, inside a transaction. The .sql text
// is compiled in through migrations.qrc under the :/migrations/ prefix.
constexpr const char *kMigrations[] = {
    "001_initial",
    "002_favorites",
    "003_search_and_notes",
    "004_embeddings",
    "005_chunk_search",
};
constexpr int kMigrationCount = sizeof(kMigrations) / sizeof(kMigrations[0]);

bool isWordChar(QChar c) {
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

// True when `keyword` (case-insensitive) occurs at `pos` in `sql` as a whole
// word -- not as a substring of a longer identifier like "APPEND" or
// "BEGINNING".
bool matchesKeywordAt(const QString &sql, int pos, const QString &keyword) {
    const int len = keyword.length();
    if (pos + len > sql.length())
        return false;
    if (sql.mid(pos, len).compare(keyword, Qt::CaseInsensitive) != 0)
        return false;
    if (pos > 0 && isWordChar(sql.at(pos - 1)))
        return false;
    if (pos + len < sql.length() && isWordChar(sql.at(pos + len)))
        return false;
    return true;
}

} // namespace

const qint64 SCHEMA_VERSION = kMigrationCount;

QStringList splitStatements(const QString &sql) {
    QStringList statements;
    QString current;
    int depth = 0; // BEGIN/END nesting depth, for CREATE TRIGGER bodies.
    bool inSingleQuote = false;
    bool inDoubleQuote = false;
    bool inLineComment = false;
    bool inBlockComment = false;

    const int n = sql.length();
    int i = 0;
    while (i < n) {
        const QChar c = sql.at(i);

        if (inLineComment) {
            current += c;
            if (c == QLatin1Char('\n'))
                inLineComment = false;
            ++i;
            continue;
        }
        if (inBlockComment) {
            current += c;
            if (c == QLatin1Char('*') && i + 1 < n && sql.at(i + 1) == QLatin1Char('/')) {
                current += sql.at(i + 1);
                ++i;
                inBlockComment = false;
            }
            ++i;
            continue;
        }
        if (inSingleQuote) {
            current += c;
            ++i;
            // '' is an escaped quote inside a string literal; only a lone
            // trailing ' closes it.
            if (c == QLatin1Char('\'')) {
                if (i < n && sql.at(i) == QLatin1Char('\'')) {
                    current += sql.at(i);
                    ++i;
                } else {
                    inSingleQuote = false;
                }
            }
            continue;
        }
        if (inDoubleQuote) {
            current += c;
            ++i;
            if (c == QLatin1Char('"')) {
                if (i < n && sql.at(i) == QLatin1Char('"')) {
                    current += sql.at(i);
                    ++i;
                } else {
                    inDoubleQuote = false;
                }
            }
            continue;
        }

        // Not inside a literal or a comment.
        if (c == QLatin1Char('\'')) {
            inSingleQuote = true;
            current += c;
            ++i;
            continue;
        }
        if (c == QLatin1Char('"')) {
            inDoubleQuote = true;
            current += c;
            ++i;
            continue;
        }
        if (c == QLatin1Char('-') && i + 1 < n && sql.at(i + 1) == QLatin1Char('-')) {
            inLineComment = true;
            current += sql.mid(i, 2);
            i += 2;
            continue;
        }
        if (c == QLatin1Char('/') && i + 1 < n && sql.at(i + 1) == QLatin1Char('*')) {
            inBlockComment = true;
            current += sql.mid(i, 2);
            i += 2;
            continue;
        }
        if (matchesKeywordAt(sql, i, QStringLiteral("BEGIN"))) {
            ++depth;
            current += sql.mid(i, 5);
            i += 5;
            continue;
        }
        if (matchesKeywordAt(sql, i, QStringLiteral("END"))) {
            if (depth > 0)
                --depth;
            current += sql.mid(i, 3);
            i += 3;
            continue;
        }
        if (c == QLatin1Char(';') && depth == 0) {
            current += c;
            statements << current;
            current.clear();
            ++i;
            continue;
        }

        current += c;
        ++i;
    }

    if (!current.trimmed().isEmpty())
        statements << current;

    return statements;
}

Result<void> migrate(QSqlDatabase &db) {
    // Scoped in braces so the statement is finalized before the loop below
    // runs any DDL: PRAGMA user_version is a row-returning statement, and
    // Qt does not finalize it until this QSqlQuery is destroyed, which
    // otherwise leaves a read lock outstanding that a later DROP/CREATE in
    // the same connection can collide with (CLAUDE.md, "Database").
    qint64 current = 0;
    {
        QSqlQuery versionQuery(db);
        if (!versionQuery.exec(QStringLiteral("PRAGMA user_version")) || !versionQuery.next())
            return Error::db(QStringLiteral("could not read PRAGMA user_version: %1")
                                  .arg(versionQuery.lastError().text()));
        current = versionQuery.value(0).toLongLong();
    }

    if (current > SCHEMA_VERSION) {
        return Error::db(
            QStringLiteral("database schema is version %1, newer than this build's %2; "
                            "refusing to downgrade")
                .arg(current)
                .arg(SCHEMA_VERSION));
    }

    for (int index = 0; index < kMigrationCount; ++index) {
        const qint64 version = index + 1;
        if (version <= current)
            continue;

        const QString name = QString::fromUtf8(kMigrations[index]);
        QFile file(QStringLiteral(":/migrations/%1.sql").arg(name));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return Error::io(QStringLiteral("missing migration resource %1").arg(file.fileName()));
        const QString sql = QString::fromUtf8(file.readAll());
        file.close();

        QSqlQuery beginTx(db);
        // BEGIN IMMEDIATE, not QSqlDatabase::transaction(), so a deferred
        // lock upgrade cannot surprise us with SQLITE_BUSY partway through a
        // migration (CLAUDE.md, "Database").
        if (!beginTx.exec(QStringLiteral("BEGIN IMMEDIATE")))
            return Error::db(beginTx.lastError().text());

        for (const QString &statement : splitStatements(sql)) {
            const QString trimmed = statement.trimmed();
            if (trimmed.isEmpty())
                continue;

            QSqlQuery step(db);
            if (!step.exec(trimmed)) {
                const QString message = step.lastError().text();
                QSqlQuery rollback(db);
                rollback.exec(QStringLiteral("ROLLBACK"));
                return Error::db(
                    QStringLiteral("migration %1 failed: %2").arg(name, message));
            }
        }

        // PRAGMA does not accept bound parameters.
        QSqlQuery bumpVersion(db);
        if (!bumpVersion.exec(QStringLiteral("PRAGMA user_version = %1").arg(version))) {
            const QString message = bumpVersion.lastError().text();
            QSqlQuery rollback(db);
            rollback.exec(QStringLiteral("ROLLBACK"));
            return Error::db(message);
        }

        QSqlQuery commit(db);
        if (!commit.exec(QStringLiteral("COMMIT")))
            return Error::db(commit.lastError().text());
    }

    return VoidResult::ok();
}
