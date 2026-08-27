#include "core/ai/ollama.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace {

const QString kDefaultUrl = QStringLiteral("http://localhost:11434");
// Measured on the reference CPU: 26 tok/s, and a usable summary in about 5
// seconds. Deliberately not a reasoning model -- a thinking model spends
// its capped output budget thinking and returns nothing.
const QString kDefaultChatModel = QStringLiteral("llama3.2:3b");
// The model medialib settled on, at 768 dimensions.
const QString kDefaultEmbedModel = QStringLiteral("nomic-embed-text");
constexpr int kEmbedDimensions = 768;
// Ceiling on generated tokens. Summaries and answers are meant to be short.
constexpr int kMaxOutputTokens = 500;

QString envOr(const char *name, const QString &fallback) {
    if (!qEnvironmentVariableIsSet(name))
        return fallback;
    return QString::fromUtf8(qgetenv(name));
}

QString normaliseUrl(const QString &url) {
    QString result = url;
    while (result.endsWith(QLatin1Char('/')))
        result.chop(1);
    return result;
}

// Blocks the calling thread until `reply` finishes. Legitimate off the GUI
// thread (CLAUDE.md, "Threading") -- these providers are called from
// worker threads running linear code.
void waitForReply(QNetworkReply *reply) {
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
}

} // namespace

Ollama::Ollama(const QString &baseUrl, const QString &chatModel, const QString &embedModel)
    : m_baseUrl(normaliseUrl(baseUrl)), m_chatModel(chatModel), m_embedModel(embedModel) { }

Ollama Ollama::fromEnv() {
    return Ollama(envOr("OLLAMA_URL", kDefaultUrl), envOr("OLLAMA_CHAT_MODEL", kDefaultChatModel),
                  envOr("OLLAMA_EMBED_MODEL", kDefaultEmbedModel));
}

QStringList Ollama::models() {
    QNetworkRequest request{QUrl(m_baseUrl + QStringLiteral("/api/tags"))};
    request.setTransferTimeout(5000);

    QNetworkReply *reply = m_nam.get(request);
    waitForReply(reply);

    const QVariant statusAttr = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (!statusAttr.isValid()) {
        reply->deleteLater();
        return QStringList();
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    reply->deleteLater();

    QStringList names;
    const QJsonArray modelsArray = doc.object().value(QStringLiteral("models")).toArray();
    for (const QJsonValue &entry : modelsArray) {
        const QString name = entry.toObject().value(QStringLiteral("name")).toString();
        if (!name.isEmpty())
            names << name;
    }
    return names;
}

bool Ollama::reachable() {
    QNetworkRequest request{QUrl(m_baseUrl + QStringLiteral("/api/tags"))};
    request.setTransferTimeout(2000);

    QNetworkReply *reply = m_nam.get(request);
    waitForReply(reply);

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();
    return status >= 200 && status < 300;
}

Result<QString> Ollama::complete(const QString &prompt) {
    QJsonObject options;
    options.insert(QStringLiteral("num_predict"), kMaxOutputTokens);
    options.insert(QStringLiteral("temperature"), 0.3);

    QJsonObject body;
    body.insert(QStringLiteral("model"), m_chatModel);
    body.insert(QStringLiteral("prompt"), prompt);
    body.insert(QStringLiteral("stream"), false);
    body.insert(QStringLiteral("options"), options);

    QNetworkRequest request{QUrl(m_baseUrl + QStringLiteral("/api/generate"))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    // A 4B-class model on a CPU is not quick; a page summary can take a
    // while and it is better to wait than to fail.
    request.setTransferTimeout(300000);

    QNetworkReply *reply = m_nam.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    waitForReply(reply);

    const QVariant statusAttr = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (!statusAttr.isValid()) {
        const QString message = reply->errorString();
        reply->deleteLater();
        return Error::net(QStringLiteral("Ollama request failed: %1").arg(message));
    }

    const int status = statusAttr.toInt();
    const QByteArray raw = reply->readAll();
    reply->deleteLater();

    if (status < 200 || status >= 300)
        return Error::net(QStringLiteral("Ollama returned %1").arg(status));

    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isObject())
        return Error::net(QStringLiteral("Ollama response was not JSON"));

    const QString text = doc.object().value(QStringLiteral("response")).toString().trimmed();
    if (text.isEmpty())
        return Error::net(QStringLiteral("Ollama returned an empty response"));

    return Result<QString>::ok(stripThinking(text));
}

bool Ollama::available() {
    return reachable();
}

ProviderClass Ollama::providerClass() const {
    return ProviderClass::Local;
}

QString Ollama::name() const {
    return QStringLiteral("ollama/%1").arg(m_chatModel);
}

Result<QVector<float>> Ollama::embed(const QString &text) {
    if (text.trimmed().isEmpty())
        return Error::net(QStringLiteral("nothing to embed"));

    QJsonObject body;
    body.insert(QStringLiteral("model"), m_embedModel);
    body.insert(QStringLiteral("prompt"), text);

    QNetworkRequest request{QUrl(m_baseUrl + QStringLiteral("/api/embeddings"))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(300000);

    QNetworkReply *reply = m_nam.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    waitForReply(reply);

    const QVariant statusAttr = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (!statusAttr.isValid()) {
        const QString message = reply->errorString();
        reply->deleteLater();
        return Error::net(QStringLiteral("Ollama embedding request failed: %1").arg(message));
    }

    const int status = statusAttr.toInt();
    const QByteArray raw = reply->readAll();
    reply->deleteLater();

    if (status < 200 || status >= 300)
        return Error::net(QStringLiteral("Ollama embedding returned %1").arg(status));

    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isObject())
        return Error::net(QStringLiteral("embedding response was not JSON"));

    const QJsonArray array = doc.object().value(QStringLiteral("embedding")).toArray();
    QVector<float> vector;
    vector.reserve(array.size());
    for (const QJsonValue &value : array)
        vector.append(static_cast<float>(value.toDouble()));

    if (vector.isEmpty())
        return Error::net(QStringLiteral("embedding response carried no vector"));

    return Result<QVector<float>>::ok(vector);
}

int Ollama::dimensions() const {
    return kEmbedDimensions;
}

QString Ollama::model() const {
    return m_embedModel;
}

QString Ollama::stripThinking(const QString &text) {
    static const QString kMarker = QStringLiteral("</think>");
    const int end = text.indexOf(kMarker);
    if (end < 0)
        return text;
    return text.mid(end + kMarker.length()).trimmed();
}
