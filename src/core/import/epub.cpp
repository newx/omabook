#include "core/import/epub.h"

#include <QtCore/private/qzipreader_p.h>

#include <QFileInfo>
#include <QLatin1String>

namespace Epub {

namespace {

// Covers larger than this are almost certainly not covers.
constexpr qint64 MAX_COVER_BYTES = 12 * 1024 * 1024;

// A single zip text entry (OPF, container.xml, one XHTML document) larger
// than this is treated as hostile: QZipReader::fileData() decompresses the
// whole entry in one call with no way to cap the stream itself, so the
// declared size is checked before that call and the actual size again
// after it (zip bomb / OOM defence).
constexpr qint64 MAX_TEXT_BYTES = 32 * 1024 * 1024;

// Upper bound on the total decompressed text pulled from one book while
// building the retrieval corpus. Stops a book full of individually-legal
// documents from summing to an unbounded allocation.
constexpr qint64 MAX_TOTAL_TEXT_BYTES = 256 * 1024 * 1024;

// Reads one zip entry as bytes, capped at MAX_TEXT_BYTES. The declared size
// in the zip's central directory is attacker-controlled, so it is only a
// cheap fast-path reject; the check on the bytes fileData() actually
// returns is the real bound.
Result<QByteArray> readZipText(const QZipReader &zip, const QString &name) {
    const QList<QZipReader::FileInfo> infos = zip.fileInfoList();
    bool found = false;
    for (const QZipReader::FileInfo &info : infos) {
        if (info.filePath != name)
            continue;
        found = true;
        if (info.size > MAX_TEXT_BYTES) {
            return Error::zip(QStringLiteral("%1 is implausibly large (over %2 bytes); refusing to read it")
                                       .arg(name)
                                       .arg(MAX_TEXT_BYTES));
        }
        break;
    }
    if (!found)
        return Error::zip(QStringLiteral("EPUB has no %1").arg(name));

    const QByteArray bytes = zip.fileData(name);
    if (bytes.size() > MAX_TEXT_BYTES) {
        return Error::zip(QStringLiteral("%1 is implausibly large (over %2 bytes); refusing to read it")
                                   .arg(name)
                                   .arg(MAX_TEXT_BYTES));
    }
    return Result<QByteArray>::ok(bytes);
}

// Reads a cover image entry, capped at MAX_COVER_BYTES. Any failure --
// missing entry, oversized, empty -- is no cover, not an error; a book
// with a broken cover still imports.
std::optional<Cover> readCover(const QZipReader &zip, const QString &path) {
    const QList<QZipReader::FileInfo> infos = zip.fileInfoList();
    bool found = false;
    for (const QZipReader::FileInfo &info : infos) {
        if (info.filePath != path)
            continue;
        found = true;
        if (info.size > MAX_COVER_BYTES)
            return std::nullopt;
        break;
    }
    if (!found)
        return std::nullopt;

    const QByteArray bytes = zip.fileData(path);
    if (bytes.isEmpty() || bytes.size() > MAX_COVER_BYTES)
        return std::nullopt;

    const int dot = path.lastIndexOf(QLatin1Char('.'));
    const QString extension = dot >= 0 ? path.mid(dot + 1).toLower() : QStringLiteral("jpg");

    return Cover{ bytes, extension };
}

} // namespace

Result<FileMetadata> readMetadata(const QString &path) {
    QZipReader zip(path);
    if (!zip.isReadable())
        return Error::zip(QStringLiteral("%1 is not a readable zip").arg(path));

    Result<QByteArray> containerResult = readZipText(zip, QStringLiteral("META-INF/container.xml"));
    if (containerResult.isErr())
        return containerResult.error();

    Result<QString> opfPathResult = containerRootfile(containerResult.value());
    if (opfPathResult.isErr())
        return opfPathResult.error();
    const QString opfPath = opfPathResult.value();

    Result<QByteArray> opfResult = readZipText(zip, opfPath);
    if (opfResult.isErr())
        return opfResult.error();
    const QByteArray opf = opfResult.value();

    Result<FileMetadata> metaResult = parseOpfMetadata(opf);
    if (metaResult.isErr())
        return metaResult.error();
    FileMetadata meta = metaResult.value();

    const std::optional<QString> href = coverHref(opf);
    if (href) {
        const QString resolved = resolveRelative(opfPath, *href);
        meta.cover = readCover(zip, resolved);
    }

    return Result<FileMetadata>::ok(meta);
}

Result<QString> extractText(const QString &path) {
    QZipReader zip(path);
    if (!zip.isReadable())
        return Error::zip(QStringLiteral("%1 is not a readable zip").arg(path));

    Result<QByteArray> containerResult = readZipText(zip, QStringLiteral("META-INF/container.xml"));
    if (containerResult.isErr())
        return containerResult.error();

    Result<QString> opfPathResult = containerRootfile(containerResult.value());
    if (opfPathResult.isErr())
        return opfPathResult.error();
    const QString opfPath = opfPathResult.value();

    Result<QByteArray> opfResult = readZipText(zip, opfPath);
    if (opfResult.isErr())
        return opfResult.error();
    const QByteArray opf = opfResult.value();

    QString out;
    qint64 total = 0;
    const QStringList hrefs = spineHrefs(opf);
    for (const QString &href : hrefs) {
        const QString resolved = resolveRelative(opfPath, href);
        const Result<QByteArray> xhtmlResult = readZipText(zip, resolved);
        if (xhtmlResult.isErr())
            continue; // one unreadable document is skipped, not fatal

        const QString text = stripMarkup(xhtmlResult.value());
        if (text.trimmed().isEmpty())
            continue;

        // Individually-legal documents can still sum to an unbounded
        // corpus; error cleanly once the running total crosses the cap so
        // the import pipeline can isolate this one book rather than
        // building an unbounded string.
        total += text.size();
        if (total > MAX_TOTAL_TEXT_BYTES) {
            return Error::zip(QStringLiteral("%1 has more text than we will index (over %2 bytes)")
                                       .arg(path)
                                       .arg(MAX_TOTAL_TEXT_BYTES));
        }

        out.append(text);
        out.append(QStringLiteral("\n\n"));
    }

    return Result<QString>::ok(out);
}

} // namespace Epub
