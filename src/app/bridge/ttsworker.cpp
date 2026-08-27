#include "ttsworker.h"

#include "core/result.h"
#include "core/tts/kokoro.h"

TtsWorker::TtsWorker(QObject *parent) : QObject(parent) { }

void TtsWorker::synthesize(int generation, int index, const QString &text, const QString &baseUrl,
                            const QString &voice, double speed) {
    // Built fresh per call rather than kept as a member: voice, speed and
    // (in principle) baseUrl can differ between one request and the next,
    // and constructing here -- inside a slot invoked on this thread --
    // guarantees the Kokoro's QNetworkAccessManager is created in the
    // thread that uses it.
    Kokoro kokoro(baseUrl, voice, speed);
    const Result<QString> result = kokoro.synthesize(text);
    if (result.isOk())
        emit chunkReady(generation, index, result.value());
    else
        emit chunkFailed(generation, index, result.error().message);
}

void TtsWorker::probeEngine(const QString &baseUrl, const QString &voice, double speed) {
    const Kokoro kokoro(baseUrl, voice, speed);
    emit engineProbed(kokoro.available());
}

void TtsWorker::fetchVoices(const QString &baseUrl, const QString &voice, double speed) {
    Kokoro kokoro(baseUrl, voice, speed);
    emit voicesFetched(kokoro.voices().join(QLatin1Char('\n')));
}
