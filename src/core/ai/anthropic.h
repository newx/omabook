// Claude, over the Messages API -- the optional remote provider. Ported
// from omabook-core/src/ai/anthropic.rs.
//
// Reachable only from interactive requests: policy::WorkPolicy refuses to
// let background work touch a remote provider, which is what keeps an
// unattended library scan from spending money.
#pragma once

#include "core/ai/provider.h"
#include "core/result.h"

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QString>
#include <optional>

class Anthropic : public ChatProvider {
public:
    Anthropic(const QString &apiKey, const QString &model);

    // Configured from the environment. Returns nullopt when no key is set,
    // which is the normal case -- the remote provider is opt-in.
    static std::optional<Anthropic> fromEnv();

    Result<QString> complete(const QString &prompt) override;
    bool available() override;
    ProviderClass providerClass() const override;
    QString name() const override;

    // Exposed for tests. `content` is a list of blocks; only the text ones
    // carry the answer.
    static QString collectText(const QJsonObject &body);

private:
    QString m_apiKey;
    QString m_model;
    // A member so it lives in the calling thread, per CLAUDE.md's
    // threading rule for providers called from worker threads in linear
    // code.
    QNetworkAccessManager m_nam;
};
