#include "core/ai/anthropic.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace {

const QString kEndpoint = QStringLiteral("https://api.anthropic.com/v1/messages");
const QString kApiVersion = QStringLiteral("2023-06-01");

// Anthropic's current default model.
const QString kDefaultModel = QStringLiteral("claude-opus-5");

// Summaries and answers about a page are short. Capping output keeps a
// runaway response from becoming a surprising line on a bill.
constexpr int kMaxTokens = 2048;

// Blocks the calling thread until `reply` finishes. Legitimate off the GUI
// thread (CLAUDE.md, "Threading") -- these providers are called from
// worker threads running linear code.
void waitForReply(QNetworkReply *reply) {
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
}

} // namespace

Anthropic::Anthropic(const QString &apiKey, const QString &model) : m_apiKey(apiKey), m_model(model) { }

std::optional<Anthropic> Anthropic::fromEnv() {
    if (!qEnvironmentVariableIsSet("ANTHROPIC_API_KEY"))
        return std::nullopt;

    const QString key = QString::fromUtf8(qgetenv("ANTHROPIC_API_KEY"));
    if (key.trimmed().isEmpty())
        return std::nullopt;

    const QString model = qEnvironmentVariableIsSet("OMABOOK_REMOTE_MODEL")
        ? QString::fromUtf8(qgetenv("OMABOOK_REMOTE_MODEL"))
        : kDefaultModel;

    // Constructed in place inside the optional's storage -- Anthropic
    // holds a QNetworkAccessManager, which is neither copyable nor
    // movable, so this must not go through a copy or move constructor.
    return std::optional<Anthropic>(std::in_place, key, model);
}

Result<QString> Anthropic::complete(const QString &prompt) {
    QJsonObject message;
    message.insert(QStringLiteral("role"), QStringLiteral("user"));
    message.insert(QStringLiteral("content"), prompt);

    QJsonArray messages;
    messages.append(message);

    QJsonObject body;
    body.insert(QStringLiteral("model"), m_model);
    body.insert(QStringLiteral("max_tokens"), kMaxTokens);
    body.insert(QStringLiteral("messages"), messages);

    QNetworkRequest request{QUrl(kEndpoint)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("x-api-key", m_apiKey.toUtf8());
    request.setRawHeader("anthropic-version", kApiVersion.toUtf8());
    request.setTransferTimeout(120000);

    QNetworkReply *reply = m_nam.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    waitForReply(reply);

    const QVariant statusAttr = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (!statusAttr.isValid()) {
        const QString message = reply->errorString();
        reply->deleteLater();
        return Error::net(QStringLiteral("Anthropic request failed: %1").arg(message));
    }

    const int status = statusAttr.toInt();
    const QByteArray raw = reply->readAll();
    reply->deleteLater();

    // Parsed regardless of status, so an error body is readable.
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isObject())
        return Error::net(QStringLiteral("Anthropic response was not JSON"));
    const QJsonObject responseBody = doc.object();

    if (status < 200 || status >= 300) {
        const QString detail = responseBody.value(QStringLiteral("error"))
                                    .toObject()
                                    .value(QStringLiteral("message"))
                                    .toString(QStringLiteral("no detail"));
        return Error::net(QStringLiteral("Anthropic returned %1: %2").arg(status).arg(detail));
    }

    // A safety refusal arrives as a normal 200; check before reading text.
    if (responseBody.value(QStringLiteral("stop_reason")).toString() == QStringLiteral("refusal"))
        return Error::net(QStringLiteral("the model declined to answer this"));

    return Result<QString>::ok(collectText(responseBody));
}

bool Anthropic::available() {
    // Deliberately not a network probe: that would be a billable-adjacent
    // request on every availability check. A key being present is the
    // signal.
    return !m_apiKey.trimmed().isEmpty();
}

ProviderClass Anthropic::providerClass() const {
    return ProviderClass::Remote;
}

QString Anthropic::name() const {
    return QStringLiteral("anthropic/%1").arg(m_model);
}

QString Anthropic::collectText(const QJsonObject &body) {
    const QJsonArray blocks = body.value(QStringLiteral("content")).toArray();
    QString result;
    for (const QJsonValue &blockValue : blocks) {
        const QJsonObject block = blockValue.toObject();
        if (block.value(QStringLiteral("type")).toString() != QStringLiteral("text"))
            continue;
        result += block.value(QStringLiteral("text")).toString();
    }
    return result.trimmed();
}
