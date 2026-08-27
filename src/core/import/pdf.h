// PDF metadata, cover, and text-quality assessment, via poppler's CLI
// tools, ported from omabook-core/src/import/pdf.rs.
//
// Shelling out rather than binding a PDF library is deliberate (SPEC §2.4):
// poppler is battle-tested C++, already a dependency, and no PDF library is
// linked (CLAUDE.md, "No third-party C++ libraries").
#pragma once

#include "core/import/metadata.h"
#include "core/models/book.h"
#include "core/result.h"

#include <QHash>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <optional>

namespace Pdf {

Result<FileMetadata> readMetadata(const QString &path);

// Extracts the text of pages [first, last] (1-indexed, inclusive) via
// `pdftotext -layout`. Empty output or a failed process is nullopt, not an
// error -- a missing poppler is a degraded feature (SPEC §6.2).
std::optional<QString> extractText(const QString &path, int first, int last);

// Judges how usable a PDF's text is by sampling three pages rather than
// reading the whole file. Gates TTS and RAG (SPEC §7.2).
TextQuality assessTextQuality(const QString &path);

std::optional<int> pageCount(const QString &path);

// --- Pure logic below, testable without a PDF -----------------------------
//
// Declared `static` and defined here so both pdf.cpp and the test binary's
// translation unit get their own working copy (CLAUDE.md, "Pure logic goes
// in static member functions" -- see epub.h for the same pattern and why).

// Whether extracted text is characters rather than words. Needs at least 40
// non-space characters to judge; below that, other checks (emptiness, the
// scanned-page threshold) already cover it, so this returns false rather
// than guessing off a fragment.
//
// Calibrated against a real 31-PDF library: every readable book scored
// word-like >= 0.68 and symbol ratio <= 0.016; the one broken book -- a PDF
// whose text is an unmapped symbol font -- scored 0.29 and 0.19. Length
// alone did not catch it.
static bool looksLikeGibberish(const QString &text) {
    constexpr double MAX_SYMBOL_RATIO = 0.08;
    constexpr double MIN_WORDLIKE_RATIO = 0.45;
    // Characters that appear mid-word when an encoding has gone wrong.
    // Ordinary mathematics uses them too, but between spaces, not inside
    // words -- see mathematicalProseIsNotGibberish in the tests.
    static const QString kGibberishSymbols = QStringLiteral("=~|\\^`");

    QString nonSpace;
    nonSpace.reserve(text.size());
    for (const QChar &c : text) {
        if (!c.isSpace())
            nonSpace.append(c);
    }
    if (nonSpace.size() < 40)
        return false;

    int symbols = 0;
    for (const QChar &c : nonSpace) {
        if (kGibberishSymbols.contains(c))
            ++symbols;
    }
    if (static_cast<double>(symbols) / nonSpace.size() > MAX_SYMBOL_RATIO)
        return true;

    const QStringList allTokens = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    QStringList tokens;
    for (const QString &token : allTokens) {
        if (token.size() >= 2)
            tokens.append(token);
    }
    if (tokens.isEmpty())
        return true;

    int wordlike = 0;
    for (const QString &token : tokens) {
        int letters = 0;
        for (const QChar &c : token) {
            if (c.isLetter())
                ++letters;
        }
        if (static_cast<double>(letters) / token.size() >= 0.8)
            ++wordlike;
    }

    return (static_cast<double>(wordlike) / tokens.size()) < MIN_WORDLIKE_RATIO;
}

// The share of lines that appear on more than one sampled page -- running
// headers, footers, and page numbers.
static double repeatedLineRatio(const QStringList &texts) {
    if (texts.size() < 2)
        return 0.0;

    QHash<QString, int> counts;
    int total = 0;
    for (const QString &text : texts) {
        const QStringList lines = text.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            const QString trimmed = line.trimmed();
            if (trimmed.size() <= 3)
                continue;
            counts[trimmed] += 1;
            ++total;
        }
    }
    if (total == 0)
        return 0.0;

    int repeated = 0;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        if (it.value() > 1)
            repeated += it.value();
    }
    return static_cast<double>(repeated) / total;
}

} // namespace Pdf
