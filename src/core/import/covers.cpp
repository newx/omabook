#include "core/import/covers.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QSaveFile>

#include <optional>

namespace {

// Covers arrive at whatever resolution the source EPUB or PDF shipped --
// sometimes a print-resolution scan several megabytes large. A library of
// a few thousand books at that size is real disk, and nothing in the
// library grid or the reader needs more than a thumbnail, so shrink on the
// way in rather than the way out. reader.size() is header-only and free,
// and for JPEG setScaledSize engages libjpeg's DCT-domain scaled decode,
// so this costs almost nothing even for a large source image.
//
// Returns nullopt when the bytes cannot be decoded at all, so the caller
// can fall back to storing them unchanged rather than losing the cover.
std::optional<QByteArray> thumbnailToJpeg(const QByteArray &bytes) {
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly))
        return std::nullopt;

    QImageReader reader(&buffer);
    reader.setAutoTransform(true); // EXIF-rotated covers come out sideways otherwise

    // setScaledSize does NOT preserve aspect ratio on its own -- the
    // target is computed by hand from the free header-only size.
    const QSize target = reader.size().scaled(QSize(320, 480), Qt::KeepAspectRatio);
    reader.setScaledSize(target);

    const QImage thumb = reader.read();
    if (thumb.isNull())
        return std::nullopt;

    QByteArray out;
    QBuffer outBuffer(&out);
    outBuffer.open(QIODevice::WriteOnly);
    if (!thumb.save(&outBuffer, "JPEG", 85))
        return std::nullopt;

    return out;
}

} // namespace

QString coversDir() {
    return Db::dataDir() + QStringLiteral("/covers");
}

QString sanitiseExtension(const QString &extension) {
    QString cleaned;
    for (const QChar &c : extension) {
        if (cleaned.length() >= 5)
            break;

        const char16_t u = c.unicode();
        const bool asciiAlnum =
            (u >= u'0' && u <= u'9') || (u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z');
        if (asciiAlnum)
            cleaned.append(c.toLower());
    }
    return cleaned.isEmpty() ? QStringLiteral("jpg") : cleaned;
}

Result<QString> store(const QString &fileHash, const Cover &cover) {
    const QString dir = coversDir();
    QDir().mkpath(dir);

    // Re-encoding always produces a JPEG, so the stored extension follows
    // the bytes actually written, not whatever the source zip claimed --
    // only the fallback path (decode failed) keeps the original, sanitised
    // extension, since those bytes are untouched.
    const std::optional<QByteArray> thumbnailed = thumbnailToJpeg(cover.bytes);
    const QByteArray bytes = thumbnailed.value_or(cover.bytes);
    const QString extension =
        thumbnailed.has_value() ? QStringLiteral("jpg") : sanitiseExtension(cover.extension);

    const QString path = dir + QLatin1Char('/') + fileHash + QLatin1Char('.') + extension;
    if (QFileInfo::exists(path))
        return Result<QString>::ok(path);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return Error::io(QStringLiteral("Could not open %1 for writing: %2").arg(path, file.errorString()));

    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return Error::io(QStringLiteral("Could not write %1").arg(path));
    }

    if (!file.commit())
        return Error::io(QStringLiteral("Could not save %1: %2").arg(path, file.errorString()));

    return Result<QString>::ok(path);
}

QString toFileUrl(const QString &path) {
    return QStringLiteral("file://") + path;
}
