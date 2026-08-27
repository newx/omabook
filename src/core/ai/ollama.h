// Ollama: the local provider, for chat and embeddings. Ported from
// omabook-core/src/ai/ollama.rs.
#pragma once

#include "core/ai/provider.h"
#include "core/result.h"

#include <QNetworkAccessManager>
#include <QString>
#include <QStringList>
#include <QVector>

class Ollama : public ChatProvider, public EmbedProvider {
public:
    Ollama(const QString &baseUrl, const QString &chatModel, const QString &embedModel);

    // Configured from the environment: OLLAMA_URL, OLLAMA_CHAT_MODEL,
    // OLLAMA_EMBED_MODEL, each defaulting when unset.
    static Ollama fromEnv();

    // The built-in defaults, exposed so the settings UI can show what it will
    // fall back to without restating the literals and drifting from them.
    static QString defaultChatModel();
    static QString defaultEmbedModel();

    // The models this Ollama has pulled; empty when it is unreachable. The
    // settings picker offers only these -- naming a model that was never
    // pulled should fail at the moment of choosing, not at the first
    // question.
    QStringList models();

    Result<QString> complete(const QString &prompt) override;
    bool available() override;
    ProviderClass providerClass() const override;
    QString name() const override;

    Result<QVector<float>> embed(const QString &text) override;
    int dimensions() const override;
    QString model() const override;

    // Exposed for tests.
    QString baseUrl() const { return m_baseUrl; }
    static QString stripThinking(const QString &text);

private:
    bool reachable();

    QString m_baseUrl;
    QString m_chatModel;
    QString m_embedModel;
    // A member so it lives in the calling thread, per CLAUDE.md's
    // threading rule for providers called from worker threads in linear
    // code.
    QNetworkAccessManager m_nam;
};
