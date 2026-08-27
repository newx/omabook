// The prompts, in one place so they can be read and changed as a set.
// Ported verbatim from omabook-core/src/ai/prompts.rs.
//
// The shared instinct: answer from the supplied text and nothing else, and
// say so when the text does not support an answer. A reading assistant
// that invents a plausible answer about a book is worse than one that
// admits it cannot tell.
#pragma once

#include <QString>

namespace Prompts {

QString pageSummary(const QString &text, const QString &book, const QString &chapter);
QString explain(const QString &text, const QString &context);

// Answering about the book being read, from retrieved passages.
QString askBook(const QString &question, const QString &passages);

// Answering about the library, from candidate books.
QString askLibrary(const QString &question, const QString &books);

} // namespace Prompts
