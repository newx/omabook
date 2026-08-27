// Database connection and schema migration, ported from
// omabook-core/src/db/mod.rs. Single responsibility: owning the connection
// and keeping the schema at the expected version. Queries live in
// src/core/repo, not here.
#pragma once

#include "core/result.h"

#include <QSqlDatabase>
#include <QString>
#include <memory>

// XDG base directories, each with an "omabook" suffix. Deliberately built
// from qEnvironmentVariable/QDir::homePath() rather than QStandardPaths, so
// a test can fake $HOME (and $XDG_*_HOME) with qputenv and get a predictable
// answer, and so the paths match the Rust build's byte for byte.
namespace Db {

QString dataDir();
QString configDir();
QString cacheDir();

} // namespace Db

// An open SQLite database with the schema migrated up to date.
//
// A QSqlDatabase connection belongs to one thread (CLAUDE.md, "Threading").
// forCurrentThread() is the path of least resistance for that rule: it opens
// one connection per OS thread, named after the thread id, and remembers it
// thread-locally for the life of the thread. Callers get a Database & and
// must never copy the QSqlDatabase out of it.
class Database {
public:
    // Lazily opens (and migrates) a connection for the calling thread on
    // first call, and returns the same one on every later call from that
    // thread. `path` only matters on the first call from a given thread --
    // in practice every thread that touches SQLite here opens the same
    // on-disk file, so that is not a real limitation. A failure to open or
    // migrate is reported with qWarning() rather than propagated, because
    // the signature mirrors a lazy accessor, not a fallible constructor;
    // the caller finds out for certain the moment a query against
    // connection() fails.
    static Database &forCurrentThread(const QString &path = defaultPath());

    // A fresh, uncached database for tests: never shares a connection with
    // forCurrentThread() or with another openForTest() call, so tests that
    // run in the same thread (QtTest runs every slot on the main thread)
    // still get independent schemas. Defaults to an in-memory database.
    // Mirrors Rust's separate `Database::open_in_memory()` constructor.
    static std::unique_ptr<Database> openForTest(const QString &path = QStringLiteral(":memory:"));

    ~Database();

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    // Never store this. Use it, then let it go out of scope.
    QSqlDatabase &connection() { return m_db; }

    // The default on-disk location: `~/.local/share/omabook/omabook.db`.
    static QString defaultPath();

    // Write a consistent snapshot without stopping the app (SPEC §5.11).
    // This is backup layer 1: it is what lets restic capture a restorable
    // database rather than a torn one.
    Result<void> vacuumInto(const QString &path) const;

private:
    explicit Database(const QString &path, const QString &connectionName);

    QSqlDatabase m_db;
    QString m_connectionName;
};
