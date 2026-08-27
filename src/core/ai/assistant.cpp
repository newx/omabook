#include "core/ai/assistant.h"

#include "core/ai/power.h"
#include "core/ai/prompts.h"
#include "core/ai/vectors.h"
#include "core/repo/bookrepository.h"

#include <QRegularExpression>

namespace {

// The C++ Error taxonomy (core/result.h) has no dedicated "Ai" kind the way
// the Rust crate's Error enum does -- every failure here is ultimately
// about a provider being unreachable, unconfigured, or given nothing
// answerable, so Net is the closest existing bucket and is what
// ai/ollama.cpp and ai/anthropic.cpp already use for provider trouble.
Error aiError(const QString &message) {
    return Error::net(message);
}

} // namespace

Scope Assistant::parseScope(const QString &text) {
    if (text == QLatin1String("book"))
        return Scope::Book;
    if (text == QLatin1String("page"))
        return Scope::Page;
    // "so far" is the safe default: it cannot reveal what is ahead.
    return Scope::SoFar;
}

QString Assistant::excerpt(const QString &text) {
    const QString flat =
            text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).join(QLatin1Char(' '));
    if (flat.size() <= 160)
        return flat;
    return flat.left(159) + QStringLiteral("…");
}

Result<ChatProvider *> Assistant::providerFor(TaskKind task, Trigger trigger) {
    const Power power = currentPower();

    // Preference is a wish, not a right: only reach for remote when it is
    // both configured/reachable and permitted for this trigger.
    if (prefers(task) == ProviderClass::Remote && m_remote && m_remote->available()) {
        const Decision decision = m_policy.permits(task, trigger, ProviderClass::Remote, power);
        if (decision.isAllowed())
            return Result<ChatProvider *>::ok(m_remote);
    }

    const Decision decision = m_policy.permits(task, trigger, ProviderClass::Local, power);
    if (decision.kind() != Decision::Kind::Allow)
        return aiError(decision.reason());

    if (!m_local.available())
        return aiError(QStringLiteral("no AI provider is available — start Ollama, or set an API key"));

    return Result<ChatProvider *>::ok(&m_local);
}

Result<Answer> Assistant::summarizePage(const QString &text, const QString &book, const QString &chapter) {
    if (text.trimmed().size() < 40)
        return aiError(QStringLiteral("there is not enough text on this page to summarize"));

    const Result<ChatProvider *> provider = providerFor(TaskKind::PageSummary, Trigger::Interactive);
    if (provider.isErr())
        return provider.error();

    const Result<QString> completion = provider.value()->complete(Prompts::pageSummary(text, book, chapter));
    if (completion.isErr())
        return completion.error();

    Answer answer;
    answer.text = completion.value();
    answer.provider = provider.value()->name();
    return Result<Answer>::ok(answer);
}

Result<Answer> Assistant::askBook(qint64 bookId, const QString &question, Scope scope, const QString &pageText,
                                   std::optional<qint64> currentOrdinal) {
    if (question.trimmed().isEmpty())
        return aiError(QStringLiteral("ask a question first"));

    QString passages;
    QStringList sources;

    if (scope == Scope::Page) {
        passages = pageText;
    } else {
        // Book sees everything; SoFar is bounded to what the reader has
        // already passed -- that bound is what keeps this scope
        // spoiler-free (SPEC §5.7).
        const std::optional<qint64> limit = scope == Scope::SoFar ? currentOrdinal : std::nullopt;

        const Result<QVector<float>> query = m_embedder.embed(question);
        if (query.isErr())
            return query.error();

        VectorStore store(m_db);
        const Result<QVector<Match>> matches =
                store.hybridInBook(bookId, question, query.value(), MAX_PASSAGES, limit);
        if (matches.isErr())
            return matches.error();

        if (matches.value().isEmpty())
            return aiError(QStringLiteral("this book has not been prepared for questions yet — index it first"));

        QStringList passageTexts;
        for (const Match &match : matches.value()) {
            passageTexts << QStringLiteral("---\n%1").arg(match.text);
            sources << excerpt(match.text);
        }
        passages = passageTexts.join(QLatin1Char('\n'));
    }

    if (passages.trimmed().isEmpty())
        return aiError(QStringLiteral("there is nothing to answer from"));

    const Result<ChatProvider *> provider = providerFor(TaskKind::Ask, Trigger::Interactive);
    if (provider.isErr())
        return provider.error();

    const Result<QString> completion = provider.value()->complete(Prompts::askBook(question, passages));
    if (completion.isErr())
        return completion.error();

    Answer answer;
    answer.text = completion.value();
    answer.provider = provider.value()->name();
    answer.sources = sources;
    return Result<Answer>::ok(answer);
}

Result<QPair<QList<qint64>, std::optional<Answer>>> Assistant::askLibrary(const QString &question) {
    using LibraryResult = Result<QPair<QList<qint64>, std::optional<Answer>>>;

    if (question.trimmed().isEmpty())
        return aiError(QStringLiteral("ask a question first"));

    QList<qint64> ids;

    const Result<QVector<float>> query = m_embedder.embed(question);
    if (query.isOk()) {
        VectorStore store(m_db);
        const Result<QVector<QPair<qint64, float>>> ranked =
                store.nearestBooks(query.value(), MAX_LIBRARY_CANDIDATES);
        if (ranked.isErr())
            return ranked.error();
        for (const auto &entry : ranked.value())
            ids << entry.first;
    }

    if (ids.isEmpty()) {
        // No embedding model, or this library has never been embedded:
        // fall back to keyword search so the books stay findable (SPEC
        // §5.8) -- the books are the product, the prose is a bonus, and
        // there is no ranked candidate set here worth asking a model to
        // reason over, so no prose is attempted in this branch.
        BookRepository repo(m_db);
        const Result<SearchResult> keyword = repo.search(question, BookSort::RecentlyAdded);
        if (keyword.isErr())
            return keyword.error();
        for (const Book &book : keyword.value().books)
            ids << book.id;
        return LibraryResult::ok(qMakePair(ids, std::optional<Answer>()));
    }

    BookRepository repo(m_db);
    QStringList listing;
    for (qint64 id : ids) {
        const Result<std::optional<Book>> found = repo.find(id);
        if (found.isOk() && found.value())
            listing << QStringLiteral("- %1 by %2").arg(found.value()->title, found.value()->authorLine());
    }

    std::optional<Answer> prose;
    const Result<ChatProvider *> provider = providerFor(TaskKind::LibraryAsk, Trigger::Interactive);
    if (provider.isOk()) {
        const Result<QString> completion =
                provider.value()->complete(Prompts::askLibrary(question, listing.join(QLatin1Char('\n'))));
        if (completion.isOk()) {
            Answer answer;
            answer.text = completion.value();
            answer.provider = provider.value()->name();
            prose = answer;
        } else {
            qInfo() << "library answer has no prose:" << completion.error().message;
        }
    } else {
        qInfo() << "library answer has no prose:" << provider.error().message;
    }

    return LibraryResult::ok(qMakePair(ids, prose));
}
