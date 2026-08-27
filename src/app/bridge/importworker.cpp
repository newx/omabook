#include "importworker.h"

#include "core/db/database.h"
#include "core/import/pipeline.h"

ImportWorker::ImportWorker(QObject *parent) : QObject(parent) { }

void ImportWorker::run(const QString &path) {
    // Opened here, inside the slot invoked on this thread, rather than
    // borrowed from the caller: forCurrentThread() opens (and remembers) a
    // connection scoped to whichever thread calls it, and WAL makes a
    // second writer on the same file safe alongside the GUI thread's reads.
    QSqlDatabase &db = Database::forCurrentThread().connection();

    const Result<ImportReport> outcome =
        importDirectory(db, path, [this](int index, int total, const QString &name) {
            emit progress(QStringLiteral("importing %1/%2 — %3")
                               .arg(index + 1)
                               .arg(total)
                               .arg(name));
        });

    const QString message = outcome.isOk()
        ? outcome.value().summary()
        : QStringLiteral("import failed: %1").arg(outcome.error().message);
    emit finished(message);
}
