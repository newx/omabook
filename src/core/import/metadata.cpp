#include "core/import/metadata.h"

#include <QFileInfo>

bool FileMetadata::isJunkTitle(const QString &title) {
    const QString t = title.trimmed();
    if (t.length() < 3)
        return true;

    const QString lower = t.toLower();
    if (lower.startsWith(QStringLiteral("pii:"))
        || lower.startsWith(QStringLiteral("the project gutenberg ebook #"))
        || lower.startsWith(QStringLiteral("microsoft word"))
        || lower.startsWith(QStringLiteral("untitled"))
        || lower.endsWith(QStringLiteral(".doc"))
        || lower.endsWith(QStringLiteral(".pdf"))
        || lower.endsWith(QStringLiteral(".indd"))
        || lower.endsWith(QStringLiteral(".qxd")))
        return true;

    // Mostly digits and punctuation with almost no letters is an
    // identifier, not a title -- this is what catches a bare ISBN.
    int letters = 0;
    for (const QChar &c : t) {
        if (c.isLetter())
            ++letters;
    }
    return letters * 2 < t.length();
}

QString FileMetadata::titleOrFilename(const QString &path) const {
    const QString trimmedTitle = title.trimmed();
    if (!trimmedTitle.isEmpty() && !isJunkTitle(trimmedTitle))
        return trimmedTitle;

    // file_stem-equivalent: strip only the final suffix, not every dot in
    // the name, then tidy the separators publishers use in place of spaces.
    QString stem = QFileInfo(path).completeBaseName();
    stem.replace(QLatin1Char('_'), QLatin1Char(' '));
    stem.replace(QLatin1Char('-'), QLatin1Char(' '));
    stem = stem.trimmed();
    if (stem.isEmpty())
        return QStringLiteral("Untitled");
    return stem;
}
