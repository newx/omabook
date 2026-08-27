// Kokoro-FastAPI speech backend, ported from omabook-core/src/tts/kokoro.rs.
//
// Kokoro-82M is a small Apache-2.0 model that synthesizes comfortably on
// CPU (SPEC §2.6). The service speaks OpenAI's audio API rather than
// anything Kokoro-specific, so this client works unchanged against any
// server implementing /v1/audio/speech.
#pragma once

#include "core/result.h"
#include "core/tts/speechbackend.h"

#include <QNetworkAccessManager>
#include <QString>
#include <QStringList>

class Kokoro : public SpeechBackend {
public:
    Kokoro(const QString &baseUrl, const QString &voice, double speed);

    // Reads KOKORO_URL, KOKORO_VOICE and KOKORO_SPEED, falling back to
    // http://localhost:8880, am_michael and 1.0 respectively.
    static Kokoro fromEnv();

    QString baseUrl() const { return m_baseUrl; }
    QString voice() const { return m_voice; }
    double speed() const { return m_speed; }

    Result<QString> synthesize(const QString &text) override;
    bool available() const override;
    QString name() const override { return QStringLiteral("kokoro"); }

    // The voices the service can synthesize; empty when it is unreachable
    // or the response is not in a shape this understands.
    QStringList voices();

    // Pulls voice names out of a /v1/audio/voices JSON body. Exposed as a
    // static function on the raw JSON text so this can be tested without a
    // server: the endpoint answers with bare names in some builds and with
    // objects in others -- kokoro-fastapi returns
    // {"voices":[{"id":"af_heart",...}]} -- and insisting on the first
    // shape returned nothing for the second, which then reported "the
    // service is not running" while it was answering every other request.
    static QStringList parseVoices(const QString &json);

    // A cache key for a chunk of text in a given voice: the first 32 hex
    // characters of SHA256(voice + '\0' + text). Exposed statically so the
    // cache path can be tested without a server; note the hash order is
    // voice-then-text even though the parameters below read text-then-voice,
    // matching the Rust original's internal ordering.
    static QString stableId(const QString &text, const QString &voice);

private:
    QString m_baseUrl;
    QString m_voice;
    double m_speed;
    // Owned by whichever thread constructs this Kokoro -- CLAUDE.md,
    // "Threading": create the QNAM in the calling thread. Mutable because
    // available() is const (it overrides SpeechBackend::available() const)
    // but issuing a GET is not a const operation on QNetworkAccessManager.
    mutable QNetworkAccessManager m_network;
};
