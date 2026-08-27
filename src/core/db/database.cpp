#include "database.h"

#include "migrations.h"

#include <QAtomicInteger>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>

namespace {

// `$XDG_..._HOME` if it is set to an absolute path, else `~/<fallback>`,
// with an "omabook" suffix in both cases -- matches
// omabook-core/src/db/mod.rs's `base_dir`.
QString baseDir(const char *envVar, const QString &fallbackRelative) {
    const QString override = qEnvironmentVariable(envVar);
    QString base = override;
    if (base.isEmpty() || !QDir::isAbsolutePath(base))
        base = QDir(QDir::homePath()).filePath(fallbackRelative);
    return QDir(base).filePath(QStringLiteral("omabook"));
}

// Make the database, and any WAL/shared-memory siblings SQLite created
// beside it, readable only by its owner.
//
// It holds an API key once one is set, and a key is worth no more than the
// file it sits in. Encrypting it here would be theatre: this is a
// single-user desktop app with nowhere to keep a decryption key that an
// attacker with read access to the file could not also reach. Permissions
// are the real boundary, so set them; a system keyring is the next step up
// (SPEC §6.2). Best-effort by design -- a filesystem without Unix modes is
// still perfectly usable.
void restrictPermissions(const QString &path) {
    const QStringList candidates = {
        path,
        path + QStringLiteral("-wal"),
        path + QStringLiteral("-shm"),
    };
    const QFile::Permissions groupOrOther = QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup
        | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther;

    for (const QString &candidate : candidates) {
        if (!QFileInfo::exists(candidate))
            continue;

        const QFile::Permissions perms = QFile::permissions(candidate);
        if (!(perms & groupOrOther))
            continue;

        if (!QFile::setPermissions(candidate, QFile::ReadOwner | QFile::WriteOwner))
            qWarning() << "could not restrict permissions on" << candidate;
    }
}

bool isInMemory(const QString &path) {
    return path == QStringLiteral(":memory:");
}

// Opens, pragmas and migrates `db` against `path`. Shared by
// forCurrentThread() and openForTest() so the two entry points cannot drift.
VoidResult openAndMigrate(QSqlDatabase &db, const QString &path) {
    if (!isInMemory(path)) {
        const QDir parent = QFileInfo(path).dir();
        if (!parent.exists() && !parent.mkpath(QStringLiteral("."))) {
            return Error::io(
                QStringLiteral("could not create directory %1").arg(parent.path()));
        }
    }

    db.setDatabaseName(path);
    // Before open(), per CLAUDE.md, "Database": a busy writer gets five
    // seconds of retrying instead of an immediate SQLITE_BUSY.
    db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    if (!db.open())
        return Error::db(db.lastError().text());

    QSqlQuery query(db);
    // WAL keeps reads from blocking on the import pipeline's writes. Not
    // available for an in-memory database, which has no journal file.
    if (!isInMemory(path) && !query.exec(QStringLiteral("PRAGMA journal_mode=WAL")))
        return Error::db(query.lastError().text());
    if (!query.exec(QStringLiteral("PRAGMA foreign_keys=ON")))
        return Error::db(query.lastError().text());
    if (!query.exec(QStringLiteral("PRAGMA synchronous=NORMAL")))
        return Error::db(query.lastError().text());

    VoidResult migrated = migrate(db);
    if (migrated.isErr())
        return migrated;

    if (!isInMemory(path))
        restrictPermissions(path);

    return VoidResult::ok();
}

} // namespace

namespace Db {

QString dataDir() {
    return baseDir("XDG_DATA_HOME", QStringLiteral(".local/share"));
}

QString configDir() {
    return baseDir("XDG_CONFIG_HOME", QStringLiteral(".config"));
}

QString cacheDir() {
    return baseDir("XDG_CACHE_HOME", QStringLiteral(".cache"));
}

} // namespace Db

QString Database::defaultPath() {
    return QDir(Db::dataDir()).filePath(QStringLiteral("omabook.db"));
}

Database::Database(const QString &path, const QString &connectionName)
    : m_db(QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName))
    , m_connectionName(connectionName) {
    const VoidResult opened = openAndMigrate(m_db, path);
    if (opened.isErr())
        qWarning() << "could not open database at" << path << ":" << opened.error().message;
}

Database::~Database() {
    // The QSqlDatabase handle referencing this connection name must be gone
    // before removeDatabase(), or it warns and leaks the handle (CLAUDE.md,
    // "Database": "Scope QSqlQuery objects in braces..."). Assigning a
    // default-constructed QSqlDatabase releases our reference.
    const QString connectionName = m_connectionName;
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
}

Database &Database::forCurrentThread(const QString &path) {
    thread_local std::unique_ptr<Database> instance;
    if (!instance) {
        const QString connectionName = QStringLiteral("omabook_%1")
                                            .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
        instance.reset(new Database(path, connectionName));
    }
    return *instance;
}

std::unique_ptr<Database> Database::openForTest(const QString &path) {
    static QAtomicInteger<quint64> counter{0};
    const QString connectionName =
        QStringLiteral("omabook_test_%1").arg(counter.fetchAndAddRelaxed(1));
    return std::unique_ptr<Database>(new Database(path, connectionName));
}

Result<void> Database::vacuumInto(const QString &path) const {
    const QDir parent = QFileInfo(path).dir();
    if (!parent.exists() && !parent.mkpath(QStringLiteral("."))) {
        return Error::io(QStringLiteral("could not create directory %1").arg(parent.path()));
    }

    if (QFile::exists(path) && !QFile::remove(path))
        return Error::io(QStringLiteral("could not remove existing snapshot at %1").arg(path));

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("VACUUM INTO ?"));
    query.addBindValue(path);
    if (!query.exec())
        return Error::db(query.lastError().text());

    return VoidResult::ok();
}
