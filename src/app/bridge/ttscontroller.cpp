#include "ttscontroller.h"
#include "ttsworker.h"

#include "core/db/database.h"
#include "core/repo/settingsrepository.h"
#include "core/result.h"
#include "core/tts/chunker.h"
#include "core/tts/kokoro.h"

#include <QThread>
#include <QtGlobal>

namespace {
const QString SETTING_VOICE = QStringLiteral("tts_voice");
const QString SETTING_SPEED = QStringLiteral("tts_speed");
// Which of the two in-flight synthesis requests a worker reply is for: the
// chunk about to play right now, or the one being readied a chunk ahead.
constexpr int PRIMARY_CHUNK = 0;
constexpr int PREFETCH_CHUNK = 1;
}

TtsController::TtsController(QObject *parent) : QObject(parent) {
    // Voice and speed: the stored setting first, then the environment
    // variable, then the built-in default. A choice made in Settings is the
    // more deliberate of the two, so it wins over KOKORO_VOICE/KOKORO_SPEED
    // rather than the reverse.
    const Kokoro envDefaults = Kokoro::fromEnv();
    m_kokoroBaseUrl = envDefaults.baseUrl();
    m_voice = envDefaults.voice();
    m_speed = envDefaults.speed();

    SettingsRepository settings(Database::forCurrentThread().connection());
    const QString storedVoice = settings.getOr(SETTING_VOICE, QString());
    if (!storedVoice.trimmed().isEmpty())
        m_voice = storedVoice;

    const QString storedSpeed = settings.getOr(SETTING_SPEED, QString());
    bool speedOk = false;
    const double parsedSpeed = storedSpeed.toDouble(&speedOk);
    if (speedOk)
        m_speed = qBound(0.5, parsedSpeed, 2.0);

    // The worker owns synthesis and the Kokoro probes so neither ever blocks
    // the GUI thread (CLAUDE.md, "Threading", pattern 1). Constructed with no
    // parent so moveToThread actually moves it rather than warning.
    auto *worker = new TtsWorker;
    m_thread = new QThread(this);
    worker->moveToThread(m_thread);

    connect(this, &TtsController::requestSynthesis, worker, &TtsWorker::synthesize);
    connect(this, &TtsController::requestEngineProbe, worker, &TtsWorker::probeEngine);
    connect(this, &TtsController::requestVoices, worker, &TtsWorker::fetchVoices);
    connect(worker, &TtsWorker::chunkReady, this, &TtsController::onWorkerChunkReady);
    connect(worker, &TtsWorker::chunkFailed, this, &TtsController::onWorkerChunkFailed);
    connect(worker, &TtsWorker::engineProbed, this, &TtsController::onEngineProbed);
    connect(worker, &TtsWorker::voicesFetched, this, &TtsController::onVoicesFetched);
    connect(m_thread, &QThread::finished, worker, &QObject::deleteLater);
    m_thread->start();

    // Correct the optimistic "kokoro" default asynchronously rather than
    // blocking here the way the Rust original's constructor did: this
    // object is built per-screen (SPEC 5.13), so a blocking probe would
    // stall opening the Reader or Settings screen for up to Kokoro's health
    // check timeout every time the service is down.
    emit requestEngineProbe(m_kokoroBaseUrl, m_voice, m_speed);
}

TtsController::~TtsController() {
    // Must stop the thread before QObject's own child cleanup deletes it --
    // deleting a running QThread warns and is unsafe.
    m_thread->quit();
    m_thread->wait();
}

void TtsController::startReading(const QString &text, bool continuous) {
    if (text.trimmed().length() < 20) {
        setStatus(QStringLiteral("nothing readable on this page"));
        return;
    }

    halt();
    setContinuous(continuous);
    setSpeaking(true);
    setStatus(QStringLiteral("preparing speech…"));

    m_chunks.clear();
    for (const QString &piece : Tts::chunk(text))
        m_chunks.enqueue(piece);
    syncChunksLeft();
    speakNext();
}

void TtsController::continueWithPage(const QString &text) {
    if (!m_speaking)
        return;

    if (text.trimmed().isEmpty()) {
        setStatus(QStringLiteral("reached the end of the book"));
        halt();
        emit finished(QStringLiteral("end-of-book"));
        return;
    }

    m_chunks.clear();
    for (const QString &piece : Tts::chunk(text))
        m_chunks.enqueue(piece);
    syncChunksLeft();
    speakNext();
}

void TtsController::chunkFinished() {
    if (!m_speaking)
        return;

    const bool exhausted = m_chunks.isEmpty() && !m_prefetched.has_value();
    if (exhausted) {
        if (m_continuous) {
            setStatus(QStringLiteral("turning the page…"));
            emit needNextPage();
        } else {
            setStatus(QStringLiteral("finished reading"));
            halt();
            emit finished(QStringLiteral("page-complete"));
        }
        return;
    }

    speakNext();
}

void TtsController::chunkFailed(const QString &reason) {
    qWarning("tts playback failed: %s", qUtf8Printable(reason));
    setStatus(QStringLiteral("playback failed: %1").arg(reason));
    halt();
    emit finished(QStringLiteral("error"));
}

void TtsController::stop() {
    halt();
    setStatus(QString());
    emit finished(QStringLiteral("stopped"));
}

void TtsController::togglePause() {
    if (!m_speaking)
        return;

    const bool paused = !m_paused;
    setPaused(paused);
    setStatus(paused ? QStringLiteral("paused") : QStringLiteral("reading aloud…"));
    emit pauseRequested(paused);
}

void TtsController::changeSpeed(double speed) {
    const double clamped = qBound(0.5, speed, 2.0);
    setSpeed(clamped);

    // Persisted like the voice, so it outlives this session too. Unlike the
    // Rust original, this never touches the voice: that version rebuilt its
    // whole backend from Kokoro::from_env() here, which silently reverted a
    // chosen voice back to the environment default on the next speed change
    // -- not replicated (see the report for this port).
    SettingsRepository settings(Database::forCurrentThread().connection());
    const Result<void> stored = settings.set(SETTING_SPEED, QString::number(clamped));
    if (stored.isErr())
        qWarning("could not persist tts speed: %s", qUtf8Printable(stored.error().message));
}

void TtsController::refreshEngine() {
    emit requestEngineProbe(m_kokoroBaseUrl, m_voice, m_speed);
}

void TtsController::refreshVoices() {
    emit requestVoices(m_kokoroBaseUrl, m_voice, m_speed);
}

void TtsController::changeVoice(const QString &voice) {
    if (voice.trimmed().isEmpty() || voice == m_voice)
        return;

    setVoice(voice);

    SettingsRepository settings(Database::forCurrentThread().connection());
    const Result<void> stored = settings.set(SETTING_VOICE, voice);
    if (stored.isErr())
        qWarning("could not persist tts voice: %s", qUtf8Printable(stored.error().message));
}

void TtsController::halt() {
    setSpeaking(false);
    setContinuous(false);
    setChunksLeft(0);
    setPaused(false);
    m_chunks.clear();
    m_prefetched.reset();
    m_pendingPrimaryText.clear();
    m_pendingPrefetchText.clear();
    ++m_generation;
    emit stopPlayback();
}

void TtsController::syncChunksLeft() {
    setChunksLeft(m_chunks.size());
}

void TtsController::speakNext() {
    if (!m_speaking)
        return;

    // A chunk already synthesized while the previous one played.
    if (m_prefetched.has_value()) {
        const QString path = *m_prefetched;
        m_prefetched.reset();
        setStatus(QStringLiteral("reading aloud…"));
        emit playAudio(path);
        prefetchNext();
        return;
    }

    if (m_chunks.isEmpty())
        return; // chunkFinished() decides what happens next

    const QString text = m_chunks.dequeue();
    syncChunksLeft();

    if (m_engine == QStringLiteral("system")) {
        setStatus(QStringLiteral("reading aloud (system voice)…"));
        emit speakSystem(text);
        return;
    }

    m_pendingPrimaryText = text;
    emit requestSynthesis(m_generation, PRIMARY_CHUNK, text, m_kokoroBaseUrl, m_voice, m_speed);
}

void TtsController::prefetchNext() {
    if (m_engine == QStringLiteral("system"))
        return;

    if (m_chunks.isEmpty())
        return;

    const QString text = m_chunks.dequeue();
    syncChunksLeft();

    m_pendingPrefetchText = text;
    emit requestSynthesis(m_generation, PREFETCH_CHUNK, text, m_kokoroBaseUrl, m_voice, m_speed);
}

void TtsController::onWorkerChunkReady(int generation, int index, const QString &path) {
    // A stale generation means the session this was requested for was
    // stopped; !m_speaking catches a halt() that landed between the request
    // and the reply. Either way the result is discarded silently on purpose
    // (SPEC 5.4) -- without this a slow Kokoro response resurrects playback
    // after the user hit Stop.
    if (generation != m_generation || !m_speaking)
        return;

    if (index == PRIMARY_CHUNK) {
        setStatus(QStringLiteral("reading aloud…"));
        emit playAudio(path);
        prefetchNext();
        return;
    }

    m_prefetched = path;
}

void TtsController::onWorkerChunkFailed(int generation, int index, const QString &reason) {
    if (generation != m_generation || !m_speaking)
        return;

    if (index == PRIMARY_CHUNK) {
        // The chunk about to play failed to synthesize -- there is no time
        // to retry it, so fall back to the system voice for this text
        // rather than going silent.
        qWarning("tts synthesis failed, falling back to the system voice: %s",
                 qUtf8Printable(reason));
        setEngine(QStringLiteral("system"));
        emit speakSystem(m_pendingPrimaryText);
        return;
    }

    // A prefetch failure has no deadline: put the chunk back at the front
    // of the queue rather than losing it.
    qWarning("tts prefetch failed, re-queuing the chunk: %s", qUtf8Printable(reason));
    m_chunks.prepend(m_pendingPrefetchText);
    syncChunksLeft();
}

void TtsController::onEngineProbed(bool available) {
    setEngine(available ? QStringLiteral("kokoro") : QStringLiteral("system"));
}

void TtsController::onVoicesFetched(const QString &voices) {
    setVoices(voices);
}

void TtsController::setSpeaking(bool speaking) {
    if (m_speaking == speaking)
        return;

    m_speaking = speaking;
    emit speakingChanged();
}

void TtsController::setContinuous(bool continuous) {
    if (m_continuous == continuous)
        return;

    m_continuous = continuous;
    emit continuousChanged();
}

void TtsController::setStatus(const QString &status) {
    if (m_status == status)
        return;

    m_status = status;
    emit statusChanged();
}

void TtsController::setEngine(const QString &engine) {
    if (m_engine == engine)
        return;

    m_engine = engine;
    emit engineChanged();
}

void TtsController::setChunksLeft(int chunksLeft) {
    if (m_chunksLeft == chunksLeft)
        return;

    m_chunksLeft = chunksLeft;
    emit chunksLeftChanged();
}

void TtsController::setPaused(bool paused) {
    if (m_paused == paused)
        return;

    m_paused = paused;
    emit pausedChanged();
}

void TtsController::setSpeed(double speed) {
    if (qFuzzyCompare(m_speed, speed))
        return;

    m_speed = speed;
    emit speedChanged();
}

void TtsController::setVoice(const QString &voice) {
    if (m_voice == voice)
        return;

    m_voice = voice;
    emit voiceChanged();
}

void TtsController::setVoices(const QString &voices) {
    if (m_voices == voices)
        return;

    m_voices = voices;
    emit voicesChanged();
}
