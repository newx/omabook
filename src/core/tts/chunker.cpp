#include "chunker.h"

namespace {

// Whether ch is a run character that follows a sentence terminator:
// another terminator, or a closing quote/bracket, both of which stay
// attached to the sentence that just ended.
bool isTrailingRunChar(QChar ch) {
    switch (ch.unicode()) {
    case '.':
    case '!':
    case '?':
    case '"':
    case '\'':
    case ')':
    case ']':
        return true;
    default:
        return false;
    }
}

// Split on sentence-ending punctuation, keeping the punctuation (and the
// space that follows it) attached to the sentence it ends, so chunks never
// start with a stray space.
//
// Indexed by QChar position rather than byte offset throughout -- Qt's
// QString is already UTF-16 code units, and every character this function
// tests for ('.', '!', '?', quotes, brackets, space) is ASCII and therefore
// exactly one code unit, so QChar indexing never lands inside a multibyte
// character the way a byte offset into UTF-8 could.
QStringList sentences(const QString &text) {
    QStringList out;
    const int length = text.length();
    int start = 0;
    int i = 0;

    while (i < length) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('.') || ch == QLatin1Char('!') || ch == QLatin1Char('?')) {
            int end = i + 1;
            while (end < length && isTrailingRunChar(text.at(end)))
                end++;
            while (end < length && text.at(end) == QLatin1Char(' '))
                end++;
            out.append(text.mid(start, end - start));
            start = end;
            i = end;
        } else {
            i++;
        }
    }

    if (start < length)
        out.append(text.mid(start));

    return out;
}

// Break a run with no sentence punctuation on word boundaries, against
// `limit` characters. Indexed by QChar (UTF-16 code unit) rather than byte,
// which is what makes this correct for multibyte scripts like Devanagari --
// a byte-oriented cut (as a naive std::string port would do) can land inside
// one character's encoding and produce invalid text or an out-of-range
// index. Every script this app targets is within the Basic Multilingual
// Plane, so a QChar is one character; this does not attempt to also handle
// surrogate pairs (emoji, some CJK Extension B ideographs).
QStringList splitLongRun(const QString &text, int limit) {
    if (text.length() <= limit)
        return QStringList{text};

    QStringList pieces;
    const int length = text.length();
    int start = 0;
    int lastSpace = -1;
    int count = 0;

    for (int index = 0; index < length; ++index) {
        const QChar ch = text.at(index);
        if (ch == QLatin1Char(' '))
            lastSpace = index;
        count++;

        if (count >= limit) {
            // Prefer a word boundary; fall back to a hard cut if this
            // single "word" is longer than the limit -- otherwise a page
            // with no spaces at all (a URL, a hash) would never advance
            // and chunk() would loop forever accumulating one giant piece.
            const int cut = (lastSpace > start) ? lastSpace + 1 : index + 1;
            pieces.append(text.mid(start, cut - start));
            start = cut;
            lastSpace = -1;
            count = 0;
            index = cut - 1; // the for loop's ++index brings this to cut
        }
    }

    if (start < length)
        pieces.append(text.mid(start));

    return pieces;
}

} // namespace

namespace Tts {

QStringList chunk(const QString &text) {
    // simplified() collapses every run of whitespace to a single space and
    // trims the ends -- the same normalization as Rust's
    // split_whitespace().join(" "). Extracted page text is full of layout
    // newlines that a speech engine would otherwise pause on.
    const QString normalized = text.simplified();
    if (normalized.isEmpty())
        return QStringList();

    QStringList chunks;
    QString current;
    int limit = FIRST_CHUNK_CHARS;

    const QStringList allSentences = sentences(normalized);
    for (const QString &sentence : allSentences) {
        // Verse, dialogue, or a poor extraction can arrive with no sentence
        // punctuation at all, making the whole page one "sentence". Those
        // are split on word boundaries instead, against the limit in force
        // now, so the first chunk stays small and playback still starts
        // quickly.
        const QStringList pieces = splitLongRun(sentence, limit);
        for (const QString &piece : pieces) {
            if (!current.isEmpty() && current.length() + piece.length() > limit) {
                chunks.append(current.trimmed());
                current.clear();
                limit = CHUNK_CHARS;
            }
            current += piece;
        }
    }

    if (!current.trimmed().isEmpty())
        chunks.append(current.trimmed());

    chunks.removeAll(QString());
    return chunks;
}

} // namespace Tts
