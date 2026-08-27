#include "core/ai/prompts.h"

namespace Prompts {

QString pageSummary(const QString &text, const QString &book, const QString &chapter) {
    QString context;
    if (!book.trimmed().isEmpty())
        context += QStringLiteral("Book: %1\n").arg(book);
    if (!chapter.trimmed().isEmpty())
        context += QStringLiteral("Chapter: %1\n").arg(chapter);

    return QStringLiteral(
               "Summarize the following passage using only the information in it.\n"
               "\n"
               "Requirements:\n"
               "- Focus on the main ideas and key arguments\n"
               "- Preserve important names, concepts, and conclusions\n"
               "- Do not add interpretation or outside knowledge\n"
               "- 3 to 5 bullet points, or at most 150 words\n"
               "\n"
               "%1"
               "\n"
               "Passage:\n"
               "%2")
        .arg(context, text);
}

QString explain(const QString &text, const QString &context) {
    return QStringLiteral(
               "Explain the following passage in plain language, as if to someone new to it.\n"
               "\n"
               "Requirements:\n"
               "- Clarify the difficult ideas without losing their meaning\n"
               "- Use short sentences\n"
               "- Avoid jargon, or explain it briefly\n"
               "- Add no new facts or opinions\n"
               "\n"
               "Context:\n"
               "%1"
               "\n"
               "\n"
               "Passage:\n"
               "%2")
        .arg(context, text);
}

QString askBook(const QString &question, const QString &passages) {
    return QStringLiteral(
               "You are a careful reading assistant.\n"
               "\n"
               "Answer the question using ONLY the passages below, which come from the book the\n"
               "reader is currently reading. If they do not clearly support an answer, say:\n"
               "\"The text does not provide enough information to answer this.\"\n"
               "\n"
               "Never mention events the passages do not contain — the reader may not have got\n"
               "there yet.\n"
               "\n"
               "Passages:\n"
               "%1"
               "\n"
               "\n"
               "Question:\n"
               "%2")
        .arg(passages, question);
}

QString askLibrary(const QString &question, const QString &books) {
    return QStringLiteral(
               "Someone asked this about their own book library: \"%1\"\n"
               "\n"
               "These are the books from that library that best matched the question:\n"
               "%2"
               "\n"
               "\n"
               "In 2 to 4 sentences, say which of them best answer the question and why. Refer\n"
               "only to books in the list. If none of them fit, say so plainly. Do not invent\n"
               "titles.")
        .arg(question, books);
}

} // namespace Prompts
