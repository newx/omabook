// What a piece of AI work is for, and the two provider interfaces that do
// it. Ported from omabook-core/src/ai/mod.rs.
//
// Two provider interfaces rather than one, because the two jobs have
// different shapes and different cost profiles: embedding is cheap, local,
// and runs in bulk; generation is expensive and, on a remote provider,
// billable. What may run unattended is decided by policy::WorkPolicy, not
// by each call site (SPEC §5.5).
#pragma once

#include "core/result.h"

#include <QString>
#include <QVector>

// What a piece of AI work is for. Routing and policy both key off this.
enum class TaskKind { Embed, Tag, PageSummary, ChapterSummary, Ask, LibraryAsk };

// Whether this task generates prose, which is the expensive kind of work.
bool isGenerative(TaskKind task);

// Where a provider runs, which is what decides whether it can cost money.
enum class ProviderClass { Local, Remote };

// The provider this task should prefer.
//
// Short, mechanical work stays local; open-ended questions prefer a
// stronger remote model when one is configured, because a small local
// model answering "what should I read next on distributed systems?" is a
// poor experience (SPEC §2.6).
ProviderClass prefers(TaskKind task);

QString toString(TaskKind task);

// Generates text.
class ChatProvider {
public:
    virtual ~ChatProvider() = default;

    virtual Result<QString> complete(const QString &prompt) = 0;
    virtual bool available() = 0;
    virtual ProviderClass providerClass() const = 0;
    virtual QString name() const = 0;
};

// Turns text into vectors.
class EmbedProvider {
public:
    virtual ~EmbedProvider() = default;

    virtual Result<QVector<float>> embed(const QString &text) = 0;
    virtual int dimensions() const = 0;
    virtual bool available() = 0;
    virtual QString model() const = 0;
};
