// TtsWorker -- the thread-owned half of TtsController.
//
// Synthesis and the Kokoro service probes are HTTP, so they run here rather
// than on the GUI thread (CLAUDE.md, "Threading", pattern 1: a worker QObject
// moved to a QThread). This object owns nothing across calls -- each request
// builds its own Kokoro, which owns its own QNetworkAccessManager, created in
// this thread because a QNetworkAccessManager belongs to the thread that
// creates it.
#pragma once

#include <QObject>
#include <QString>

class TtsWorker : public QObject {
    Q_OBJECT

public:
    // No parent: TtsController::moveToThread()s this to its worker thread,
    // and an object that has a parent stays on the creating thread --
    // moveToThread only warns (CLAUDE.md, "Traps").
    explicit TtsWorker(QObject *parent = nullptr);

public slots:
    // Synthesize `text` in `voice` at `speed` against the service at
    // `baseUrl`. `generation` and `index` are opaque to this object --
    // echoed back unchanged so the controller can match the reply to the
    // request that produced it (a stale generation means the session was
    // stopped; `index` tells the chunk meant for immediate playback apart
    // from the one being prefetched a chunk ahead).
    void synthesize(int generation, int index, const QString &text, const QString &baseUrl,
                     const QString &voice, double speed);

    // GET /health.
    void probeEngine(const QString &baseUrl, const QString &voice, double speed);

    // GET /v1/audio/voices.
    void fetchVoices(const QString &baseUrl, const QString &voice, double speed);

signals:
    void chunkReady(int generation, int index, const QString &path);
    void chunkFailed(int generation, int index, const QString &reason);
    void engineProbed(bool available);
    void voicesFetched(const QString &voices);
};
