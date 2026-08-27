#include "assets.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include "core/db/database.h"

namespace {

bool holdsReader(const QString &directory) {
    return QFileInfo::exists(directory + QStringLiteral("/reader.html"));
}

// Walk up from the executable looking for a marker, for a build that still
// sits inside its source tree. Five levels covers build/, build-debug/, and a
// shadow build a directory or two further out.
QString ancestorHolding(const QString &relativeMarker) {
    QDir dir(QCoreApplication::applicationDirPath());
    for (int level = 0; level < 5; ++level) {
        if (QFileInfo::exists(dir.filePath(relativeMarker)))
            return dir.absolutePath();
        if (!dir.cdUp())
            break;
    }
    return {};
}

} // namespace

namespace Assets {

QString readerDir() {
    const QString overridden = qEnvironmentVariable("OMABOOK_READER_DIR");
    if (!overridden.isEmpty()) {
        if (holdsReader(overridden))
            return overridden;
        qWarning("OMABOOK_READER_DIR has no reader.html: %s", qUtf8Printable(overridden));
    }

    const QString installed = QStringLiteral("/usr/share/omabook/reader");
    if (holdsReader(installed))
        return installed;

    const QString local = Db::dataDir() + QStringLiteral("/reader");
    if (holdsReader(local))
        return local;

    const QString source = ancestorHolding(QStringLiteral("assets/reader/reader.html"));
    if (!source.isEmpty())
        return source + QStringLiteral("/assets/reader");

    return {};
}

QString readerUrl(const QString &bookPath, const QString &cfi) {
    const QString dir = readerDir();
    if (dir.isEmpty()) {
        qWarning("no reader assets found; a book cannot be opened");
        return {};
    }

    // Book paths come from the filesystem and CFIs are full of ()!/ , so
    // neither may be interpolated raw: one space or ampersand truncates the
    // query string. QUrl::toPercentEncoding keeps exactly A-Za-z0-9-_.~
    // literal and hex-encodes every other byte, UTF-8 included.
    QString url = QStringLiteral("file://%1/reader.html?file=%2")
        .arg(dir, QString::fromLatin1(QUrl::toPercentEncoding(
            QStringLiteral("file://") + bookPath)));

    if (!cfi.isEmpty()) {
        url += QStringLiteral("&cfi=");
        url += QString::fromLatin1(QUrl::toPercentEncoding(cfi));
    }

    return url;
}
} // namespace Assets
