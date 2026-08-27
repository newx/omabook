#include "core/import/scanner.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace {

// Files below this are truncated downloads or placeholders, not books.
constexpr qint64 kMinPlausibleBytes = 1024;

// Any path component starting with '.' marks a hidden file, or the
// dotfile directory it lives in -- both are noise, not books.
bool hasDotfileComponent(const QString &path) {
    const QStringList components = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &component : components) {
        if (component.startsWith(QLatin1Char('.')))
            return true;
    }
    return false;
}

void walk(const QString &dirPath, QVector<Candidate> &found) {
    const QFileInfo dirInfo(dirPath);
    if (!dirInfo.exists())
        return;

    if (!dirInfo.isReadable()) {
        qWarning() << "skipping unreadable entry during scan:" << dirPath;
        return;
    }

    const QFileInfoList entries = QDir(dirPath).entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::NoSort);

    for (const QFileInfo &entry : entries) {
        // Not following symlinks, and a symlink is not treated as a file
        // either -- this matches WalkDir::follow_links(false), which
        // reports a symlink's own type rather than chasing its target.
        if (entry.isSymLink())
            continue;

        if (hasDotfileComponent(entry.filePath()))
            continue;

        if (entry.isDir()) {
            walk(entry.filePath(), found);
            continue;
        }

        if (!entry.isFile())
            continue;

        const std::optional<BookFormat> format = bookFormatFromExtension(entry.suffix());
        if (!format.has_value())
            continue;

        const qint64 size = entry.size();
        if (size < kMinPlausibleBytes)
            continue;

        found.append(Candidate{entry.filePath(), *format, size});
    }
}

} // namespace

QVector<Candidate> scan(const QString &root) {
    QVector<Candidate> found;
    walk(root, found);

    // Stable order makes imports reproducible and progress reporting sane.
    std::sort(found.begin(), found.end(),
        [](const Candidate &a, const Candidate &b) { return a.path < b.path; });
    return found;
}
