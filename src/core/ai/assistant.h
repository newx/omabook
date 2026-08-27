// The AI features themselves: summaries, questions about a book, and
// questions about the library (SPEC §5.6-5.8). Ported from
// omabook-core/src/ai/assistant.rs.
//
// Every call states its Trigger. Nothing here decides for itself whether it
// is allowed to run -- that is WorkPolicy's job -- and nothing runs
// unattended against a remote provider.
#pragma once

#include "core/ai/policy.h"
#include "core/ai/provider.h"
#include "core/result.h"

#include <QPair>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

#include <optional>

// How much of a book a question may see.
enum class Scope { Page, SoFar, Book };

// How many passages to put in front of the model. Measured on Moby-Dick
// (1199 chunks) with llama3.2:3b locally: four passages answer in 1-3s but
// too often report that the text does not say; eight answer in about 10s
// and actually answer.
constexpr int MAX_PASSAGES = 8;
// How many books a library question considers.
constexpr int MAX_LIBRARY_CANDIDATES = 20;

// An answer, and what produced it.
struct Answer {
    QString text;
    QString provider; // which provider answered, for the UI to show
    QStringList sources; // passages used, so an answer can be traced back to the book
};

class Assistant {
public:
    // Takes every collaborator by reference/pointer rather than owning one,
    // mirroring the Rust type's borrowed fields: a QSqlDatabase connection
    // belongs to one thread (CLAUDE.md, "Threading"), so an Assistant must
    // never outlive the call that constructed it. `remote` may be null --
    // the app runs perfectly well with only a local provider configured.
    Assistant(QSqlDatabase &db, ChatProvider &local, ChatProvider *remote, EmbedProvider &embedder,
              WorkPolicy policy)
        : m_db(db), m_local(local), m_remote(remote), m_embedder(embedder), m_policy(policy) { }

    // Summarize the page on screen. Always interactive: someone pressed a
    // button and is waiting.
    Result<Answer> summarizePage(const QString &text, const QString &book, const QString &chapter);

    // Answer a question about the book being read.
    Result<Answer> askBook(qint64 bookId, const QString &question, Scope scope, const QString &pageText,
                            std::optional<qint64> currentOrdinal);

    // Answer a question about the library -- "what maths books do I have?".
    // Degrades on purpose: with no embeddings or no model, the ranked books
    // still come back from a keyword search with no prose, because the
    // books are the answer and the prose is a bonus (SPEC §5.8).
    Result<QPair<QList<qint64>, std::optional<Answer>>> askLibrary(const QString &question);

    // "book" -> Book, "page" -> Page, anything else -> SoFar: SoFar is the
    // scope that cannot leak ahead of the reader, so an unrecognised value
    // must fall to the safe one.
    static Scope parseScope(const QString &text);

    // A short, readable piece of a passage, for showing where an answer
    // came from. Testable on its own (CLAUDE.md, "Pure logic goes in
    // static member functions").
    static QString excerpt(const QString &text);

private:
    // Choose a provider for a task, honouring both preference and policy.
    // Preference is a wish, not a right: a task that would prefer remote is
    // given local, or nothing, when policy says no.
    Result<ChatProvider *> providerFor(TaskKind task, Trigger trigger);

    QSqlDatabase &m_db;
    ChatProvider &m_local;
    ChatProvider *m_remote;
    EmbedProvider &m_embedder;
    WorkPolicy m_policy;
};
