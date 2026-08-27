#include "kokoro.h"

#include "core/db/database.h"

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrl>

namespace {

const QString DEFAULT_URL = QStringLiteral("http://localhost:8880");
// The pick of the American male voices in Kokoro's own published grades.
const QString DEFAULT_VOICE = QStringLiteral("am_michael");

// Kokoro applies speed during synthesis, so a value outside its supported
// range would either be silently clamped by the server or rejected outright
// depending on build; clamping here gives a predictable result either way.
double clampSpeed(double speed) {
    return qBound(0.5, speed, 2.0);
}

// Blocks the calling thread until `reply` finishes. Legitimate off the GUI
// thread (CLAUDE.md, "Threading", pattern 3): synthesize() and voices() run
// from the TTS worker thread in linear code, and a local QEventLoop is
// simpler than turning this file into a state machine for one blocking call.
void waitForFinished(QNetworkReply *reply) {
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
}

} // namespace

Kokoro::Kokoro(const QString &baseUrl, const QString &voice, double speed)
    : m_baseUrl(baseUrl)
    , m_voice(voice)
    , m_speed(clampSpeed(speed)) {
    while (m_baseUrl.endsWith(QLatin1Char('/')))
        m_baseUrl.chop(1);
}

Kokoro Kokoro::fromEnv() {
    QString url = qEnvironmentVariable("KOKORO_URL");
    if (url.isEmpty())
        url = DEFAULT_URL;

    QString voice = qEnvironmentVariable("KOKORO_VOICE");
    if (voice.isEmpty())
        voice = DEFAULT_VOICE;

    bool speedOk = false;
    const double speed = qEnvironmentVariable("KOKORO_SPEED").toDouble(&speedOk);

    return Kokoro(url, voice, speedOk ? speed : 1.0);
}

bool Kokoro::available() const {
    QNetworkRequest request((QUrl(m_baseUrl + QStringLiteral("/health"))));
    request.setTransferTimeout(2000);

    QNetworkReply *reply = m_network.get(request);
    waitForFinished(reply);

    // "Any non-error status counts": QNetworkAccessManager maps a non-2xx
    // HTTP response to a NetworkError, so checking error() alone is enough
    // and matches the Rust original's status().is_success() check without
    // reading the status code by hand.
    const bool ok = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();
    return ok;
}

QStringList Kokoro::voices() {
    QNetworkRequest request((QUrl(m_baseUrl + QStringLiteral("/v1/audio/voices"))));
    request.setTransferTimeout(5000);

    QNetworkReply *reply = m_network.get(request);
    waitForFinished(reply);

    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        return QStringList();
    }

    const QString body = QString::fromUtf8(reply->readAll());
    reply->deleteLater();
    return parseVoices(body);
}

QStringList Kokoro::parseVoices(const QString &json) {
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject())
        return QStringList();

    const QJsonValue voicesValue = doc.object().value(QStringLiteral("voices"));
    if (!voicesValue.isArray())
        return QStringList();

    QStringList result;
    const QJsonArray array = voicesValue.toArray();
    for (const QJsonValue &entry : array) {
        if (entry.isString()) {
            result.append(entry.toString());
            continue;
        }

        if (!entry.isObject())
            continue;

        // Some builds answer with bare strings, kokoro-fastapi answers with
        // {"id": ..., "name": ...} objects. Prefer "id", fall back to
        // "name", and skip an entry with neither rather than guessing.
        const QJsonObject object = entry.toObject();
        QJsonValue name = object.value(QStringLiteral("id"));
        if (!name.isString())
            name = object.value(QStringLiteral("name"));
        if (name.isString())
            result.append(name.toString());
    }
    return result;
}

QString Kokoro::stableId(const QString &text, const QString &voice) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(voice.toUtf8());
    hash.addData("\0", 1);
    hash.addData(text.toUtf8());
    return QString::fromLatin1(hash.result().toHex()).left(32);
}

Result<QString> Kokoro::synthesize(const QString &text) {
    // Checked before any request is issued: an unreachable Kokoro would
    // otherwise make every blank paragraph in a book wait out the full
    // network timeout before failing.
    if (text.trimmed().isEmpty())
        return Error::net(QStringLiteral("nothing to synthesize"));

    const QJsonObject payload{
        {QStringLiteral("model"), QStringLiteral("kokoro")},
        {QStringLiteral("input"), text},
        {QStringLiteral("voice"), m_voice},
        {QStringLiteral("response_format"), QStringLiteral("wav")},
        {QStringLiteral("speed"), m_speed},
    };

    QNetworkRequest request((QUrl(m_baseUrl + QStringLiteral("/v1/audio/speech"))));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    // Synthesis is proportional to text length, and a page of a book is
    // not a short prompt.
    request.setTransferTimeout(180000);

    QNetworkReply *reply = m_network.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    waitForFinished(reply);

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool success = reply->error() == QNetworkReply::NoError && statusCode >= 200 && statusCode < 300;

    if (!success) {
        // A status code of 0 means the request never reached the server
        // (connection refused, DNS failure, timeout) -- there is no body to
        // parse, so report the transport error directly.
        if (statusCode == 0) {
            const QString message = reply->errorString();
            reply->deleteLater();
            return Error::net(QStringLiteral("Kokoro request failed: %1").arg(message));
        }

        const QByteArray body = reply->readAll();
        reply->deleteLater();

        // Errors come back as JSON with the useful part under "detail" -- an
        // unknown voice is reported there with the valid list. A JSON
        // string is unwrapped so the message reads as a sentence rather
        // than a quoted literal; anything else is re-serialized as-is.
        QString detail;
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        const QJsonValue detailValue = doc.isObject() ? doc.object().value(QStringLiteral("detail")) : QJsonValue();
        if (detailValue.isString()) {
            detail = detailValue.toString();
        } else if (!detailValue.isUndefined()) {
            // QJsonDocument can only serialize an object or array at the
            // top level, so wrap the scalar/object/array in a one-element
            // array and strip the brackets back off.
            QJsonArray wrapper;
            wrapper.append(detailValue);
            const QString serialized =
                QString::fromUtf8(QJsonDocument(wrapper).toJson(QJsonDocument::Compact));
            detail = serialized.mid(1, serialized.length() - 2);
        } else {
            detail = QString::fromUtf8(body);
        }

        return Error::net(QStringLiteral("Kokoro returned %1: %2").arg(statusCode).arg(detail));
    }

    const QByteArray bytes = reply->readAll();
    reply->deleteLater();

    const QString dirPath = Db::cacheDir() + QStringLiteral("/tts");
    if (!QDir().mkpath(dirPath))
        return Error::io(QStringLiteral("could not create directory %1").arg(dirPath));

    // Named by content so an identical chunk is never synthesized twice --
    // re-reading a page is common.
    const QString path = dirPath + QLatin1Char('/') + stableId(text, m_voice) + QStringLiteral(".wav");

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return Error::io(QStringLiteral("could not open %1 for writing").arg(path));

    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return Error::io(QStringLiteral("could not write %1").arg(path));
    }

    if (!file.commit())
        return Error::io(QStringLiteral("could not save %1").arg(path));

    return Result<QString>::ok(path);
}
