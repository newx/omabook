// TtsController -- page-by-page reading aloud, with autoplay (SPEC 5.4).
//
// This object owns the loop: chunking, synthesizing one chunk ahead of
// playback, and deciding when the page is exhausted. QML owns only playback
// -- the media player and the system speech engine are QML types (Reader.qml)
// -- so this controller never touches QtMultimedia; it only emits paths and
// text for QML to play.
#pragma once

#include <QObject>
#include <QQueue>
#include <QString>
#include <optional>

class QThread;
class TtsWorker;

class TtsController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool speaking READ speaking NOTIFY speakingChanged)
    Q_PROPERTY(bool continuous READ continuous NOTIFY continuousChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // "kokoro" when the service answers, otherwise "system".
    Q_PROPERTY(QString engine READ engine NOTIFY engineChanged)
    Q_PROPERTY(int chunksLeft READ chunksLeft NOTIFY chunksLeftChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(double speed READ speed NOTIFY speedChanged)
    Q_PROPERTY(QString voice READ voice NOTIFY voiceChanged)
    // Every voice the service offers, newline-separated for QML.
    Q_PROPERTY(QString voices READ voices NOTIFY voicesChanged)

public:
    explicit TtsController(QObject *parent = nullptr);
    ~TtsController() override;

    bool speaking() const { return m_speaking; }
    bool continuous() const { return m_continuous; }
    QString status() const { return m_status; }
    QString engine() const { return m_engine; }
    int chunksLeft() const { return m_chunksLeft; }
    bool paused() const { return m_paused; }
    double speed() const { return m_speed; }
    QString voice() const { return m_voice; }
    QString voices() const { return m_voices; }

    // Begin reading `text`. `continuous` turns pages until the book ends.
    Q_INVOKABLE void startReading(const QString &text, bool continuous);

    // Feed the next page's text after needNextPage(). Empty text ends the
    // session -- that is how "reached the end of the book" is detected.
    Q_INVOKABLE void continueWithPage(const QString &text);

    // QML calls this when the current chunk finishes playing.
    Q_INVOKABLE void chunkFinished();

    // QML calls this when playback of a chunk fails -- not to be confused
    // with a synthesis failure, which never reaches QML (onWorkerChunkFailed
    // handles that internally, by falling back to the system voice or
    // re-queuing, before QML ever sees a chunk).
    Q_INVOKABLE void chunkFailed(const QString &reason);

    Q_INVOKABLE void stop();

    // Pause or resume playback. Synthesis already in flight is kept, so
    // resuming does not re-synthesize.
    Q_INVOKABLE void togglePause();

    // Playback rate, clamped to 0.5-2.0. Applied to chunks synthesized from
    // now on; a chunk already requested keeps the rate it was requested
    // with.
    //
    // Not named setSpeed: a `speed` property with a WRITE accessor would
    // have moc generate a setSpeed of its own, and the overload would be
    // ambiguous to moc. Same constraint, same reason, as the Rust original
    // this replaces (omabook/crates/omabook-app/src/bridge/tts.rs) -- keep
    // the name if a WRITE accessor is ever added to `speed`.
    Q_INVOKABLE void changeSpeed(double speed);

    // Re-probe the speech service, e.g. after starting the container.
    Q_INVOKABLE void refreshEngine();

    // Ask the service which voices it has. Off the UI thread: this is an
    // HTTP call, and an unreachable service takes its timeout to say so.
    Q_INVOKABLE void refreshVoices();

    // Read in a different voice from here on. A chunk already requested
    // keeps the voice it was requested with -- voice is captured into the
    // synthesis request at the moment it is made, not read back out of this
    // controller later.
    //
    // Not named setVoice, for the same moc-ambiguity reason as changeSpeed.
    Q_INVOKABLE void changeVoice(const QString &voice);

signals:
    // Play this audio file (Kokoro path).
    void playAudio(const QString &path);
    // Speak this text with the system engine (no synthesis backend).
    void speakSystem(const QString &text);
    // The page is exhausted and autoplay is on: turn the page.
    void needNextPage();
    void finished(const QString &reason);
    // Pause or resume the player QML owns.
    void pauseRequested(bool paused);
    // Silence the player QML owns, now. Ending the loop is not enough: the
    // chunk already handed over would otherwise play to its natural end --
    // up to 600 characters, tens of seconds.
    void stopPlayback();

    void speakingChanged();
    void continuousChanged();
    void statusChanged();
    void engineChanged();
    void chunksLeftChanged();
    void pausedChanged();
    void speedChanged();
    void voiceChanged();
    void voicesChanged();

private:
    void setSpeaking(bool speaking);
    void setContinuous(bool continuous);
    void setStatus(const QString &status);
    void setEngine(const QString &engine);
    void setChunksLeft(int chunksLeft);
    void setPaused(bool paused);
    void setSpeed(double speed);
    void setVoice(const QString &voice);
    void setVoices(const QString &voices);

    // Clear all session state. Bumping the generation makes any synthesis
    // still in flight discard its result when it lands, and stopPlayback()
    // cuts off the chunk QML is playing right now (SPEC 5.4).
    void halt();

    void syncChunksLeft();

    // Emit the next chunk for playback, and kick off synthesis of the one
    // after it.
    void speakNext();

    // Synthesize the next chunk while the current one plays. This is what
    // keeps the initial wait to a few seconds rather than a minute.
    void prefetchNext();

private slots:
    void onWorkerChunkReady(int generation, int index, const QString &path);
    void onWorkerChunkFailed(int generation, int index, const QString &reason);
    void onEngineProbed(bool available);
    void onVoicesFetched(const QString &voices);

signals:
    // Plumbing to the worker thread, not part of the QML-facing API above.
    // A cross-thread signal-to-slot connection is the only channel to the
    // worker (CLAUDE.md, "Threading") -- no shared mutable state. Every
    // parameter is a primitive or QString, so none of this needs
    // qRegisterMetaType.
    void requestSynthesis(int generation, int index, QString text, QString baseUrl,
                           QString voice, double speed);
    void requestEngineProbe(QString baseUrl, QString voice, double speed);
    void requestVoices(QString baseUrl, QString voice, double speed);

private:
    bool m_speaking = false;
    bool m_continuous = false;
    QString m_status;
    // Optimistic: corrected within a probe round-trip by the constructor's
    // refreshEngine()-equivalent call, and self-heals even before that
    // resolves, because a failed synthesis of the chunk about to play falls
    // back to the system voice (onWorkerChunkFailed).
    QString m_engine = QStringLiteral("kokoro");
    int m_chunksLeft = 0;
    bool m_paused = false;
    double m_speed = 1.0;
    QString m_voice;
    QString m_voices;

    QQueue<QString> m_chunks;
    // The chunk synthesized one ahead of what is currently playing -- the
    // "one ahead" that keeps playback from ever waiting on synthesis.
    std::optional<QString> m_prefetched;
    // Bumped on every halt(), so a synthesis result from an abandoned
    // session is discarded -- silently -- when it eventually arrives
    // (SPEC 5.4).
    int m_generation = 0;
    // Fixed for the life of this controller; only KOKORO_URL changes it.
    QString m_kokoroBaseUrl;

    // The text behind whichever request is currently in flight, so the
    // worker's reply -- which carries only a generation, an index and a
    // path or reason -- can be matched back to what was actually asked for.
    QString m_pendingPrimaryText;
    QString m_pendingPrefetchText;

    QThread *m_thread;
};
