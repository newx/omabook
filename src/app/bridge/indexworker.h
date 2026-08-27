// IndexWorker -- the thread-owned half of AiController's indexing jobs.
//
// Preparing a book (or a whole library) for questions is long, stateful and
// cancellable, so it runs on its own QThread rather than through
// QtConcurrent (CLAUDE.md, "Threading", pattern 1). A fresh worker and
// thread are created per run by AiController rather than kept alive for the
// controller's whole life, mirroring the Rust original spawning a fresh
// std::thread for each index_library()/index_book() call.
#pragma once

#include <QObject>
#include <QString>

#include <atomic>

class IndexWorker : public QObject {
    Q_OBJECT

public:
    // No parent: AiController moveToThread()s this to a worker thread, and
    // an object that has a parent stays on the creating thread --
    // moveToThread only warns (CLAUDE.md, "Traps").
    explicit IndexWorker(QObject *parent = nullptr);

    // Stop between books or between chunks; work already done is kept.
    // Thread-safe by construction (std::atomic_bool) so the GUI thread may
    // call this directly on an object living in the worker thread --
    // CLAUDE.md, "Threading": "written directly from the GUI thread".
    void cancel();

public slots:
    // Index every book that has none, one at a time. The work policy is
    // re-checked before each book (not once at the start) so unplugging
    // mid-run is respected; `backgroundEnabled`/`backgroundOnBattery` are
    // passed as primitives rather than a WorkPolicy struct so nothing here
    // needs qRegisterMetaType (CLAUDE.md, "Threading").
    void runLibrary(bool backgroundEnabled, bool backgroundOnBattery);

    // Prepare one book: chunk it, then embed it. No work-policy check --
    // this is always an explicit, interactive action, unlike runLibrary.
    void runBook(qint64 bookId);

signals:
    // Library run only: which book is being worked on right now.
    void statusChanged(const QString &status);
    // Book run only: chunks embedded so far, and how many there are.
    void progressChanged(int done, int total);
    // Either run: what to show once the job stops, whichever way it stopped.
    void finished(const QString &message);

private:
    std::atomic_bool m_keepGoing { true };
};
