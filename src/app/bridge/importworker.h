// ImportWorker -- the thread-owned half of LibraryModel::importDirectory.
//
// Long and stateful, so it runs on a worker QObject moved to a QThread
// (CLAUDE.md, "Threading", pattern 1), not on the GUI thread. It opens its
// own database connection inside that thread -- a QSqlDatabase connection
// belongs to one thread, and this is the single easiest way to corrupt the
// app otherwise.
#pragma once

#include <QObject>
#include <QString>

class ImportWorker : public QObject {
    Q_OBJECT

public:
    // No parent: LibraryModel::moveToThread()s this to the import thread,
    // and an object that has a parent stays on the creating thread --
    // moveToThread only warns (CLAUDE.md, "Traps").
    explicit ImportWorker(QObject *parent = nullptr);

public slots:
    // Import every book under `path`. Runs the whole pipeline linearly on
    // this thread.
    void run(const QString &path);

signals:
    // One report per file, emitted before that file is imported, so the UI
    // can show progress on a large library.
    void progress(const QString &text);
    // The final, human-readable summary of the run -- or the failure
    // reason if the run could not even start.
    void finished(const QString &message);
};
