#include "core/import/mobi.h"

#include <QtEndian>

#include <QFile>

namespace Mobi {

namespace {

// PalmDB header: 78 bytes, record count at 76, entries of 8 bytes after it.
constexpr qint64 PALM_HEADER_LEN = 78;
// PalmDOC header inside record 0, followed by the MOBI header.
constexpr qint64 PALMDOC_HEADER_LEN = 16;
// The MOBI header flag saying an EXTH block follows it.
constexpr quint32 EXTH_PRESENT = 0x40;

// EXTH record types, from the MobileRead wiki.
constexpr quint32 EXTH_AUTHOR = 100;
constexpr quint32 EXTH_PUBLISHER = 101;
constexpr quint32 EXTH_DESCRIPTION = 103;
constexpr quint32 EXTH_ISBN = 104;
constexpr quint32 EXTH_SUBJECT = 105;
constexpr quint32 EXTH_PUBLISH_DATE = 106;
constexpr quint32 EXTH_TITLE = 503;
constexpr quint32 EXTH_LANGUAGE = 524;

// Reads a big-endian quint32 at `at`, or nullopt if that would read past
// the end of `bytes`. Every offset in this file comes either from a fixed
// header position or from a value read out of the file itself, so nothing
// here may be trusted without this check.
std::optional<quint32> beU32(const QByteArray &bytes, qint64 at) {
    if (at < 0 || at + 4 > bytes.size())
        return std::nullopt;
    return qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(bytes.constData() + at));
}

std::optional<quint16> beU16(const QByteArray &bytes, qint64 at) {
    if (at < 0 || at + 2 > bytes.size())
        return std::nullopt;
    return qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(bytes.constData() + at));
}

// 65001 is UTF-8; the only other value in the wild is 1252, which for the
// titles and names that reach here is Latin-1 in all but a few glyphs. Each
// byte maps directly to the code point of the same value -- not a real
// cp1252 table -- matching the Rust reference's `b as char`.
QString decodeText(const QByteArray &raw, quint32 encoding) {
    if (encoding == 65001)
        return QString::fromUtf8(raw);
    return QString::fromLatin1(raw);
}

// Trims whitespace and NUL bytes (MOBI records are commonly NUL-padded).
// Returns an empty string for "nothing here", which the caller treats the
// same way FileMetadata treats any other unset field.
QString clean(const QString &text) {
    const auto isTrimmable = [](QChar c) { return c.isSpace() || c == QChar(QChar::Null); };
    qsizetype start = 0;
    qsizetype end = text.size();
    while (start < end && isTrimmable(text.at(start)))
        ++start;
    while (end > start && isTrimmable(text.at(end - 1)))
        --end;
    return text.mid(start, end - start);
}

// Walks the EXTH block, filling in whichever of `meta`'s fields have a
// record. A `len` under 8 breaks the loop rather than looping forever --
// that is the guard against a corrupt length spinning on a truncated or
// hostile file.
void readExth(const QByteArray &block, FileMetadata &meta, quint32 encoding) {
    if (block.size() < 4 || block.left(4) != QByteArrayLiteral("EXTH"))
        return;
    const std::optional<quint32> count = beU32(block, 8);
    if (!count)
        return;

    qint64 pos = 12;
    for (quint32 i = 0; i < *count; ++i) {
        const std::optional<quint32> kind = beU32(block, pos);
        const std::optional<quint32> len = beU32(block, pos + 4);
        if (!kind || !len)
            break;
        if (*len < 8)
            break; // a corrupt length would loop forever

        const qint64 dataStart = pos + 8;
        const qint64 dataEnd = pos + static_cast<qint64>(*len);
        if (dataStart < 0 || dataEnd > block.size())
            break;

        const QString text = clean(decodeText(block.mid(dataStart, dataEnd - dataStart), encoding));
        pos += static_cast<qint64>(*len);

        if (text.isEmpty())
            continue;

        switch (*kind) {
        case EXTH_AUTHOR: meta.authors.append(text); break;
        case EXTH_PUBLISHER: meta.publisher = text; break;
        case EXTH_DESCRIPTION: meta.description = text; break;
        case EXTH_ISBN: meta.isbn13 = text; break;
        case EXTH_SUBJECT: meta.subjects.append(text); break;
        case EXTH_PUBLISH_DATE: meta.publishedDate = text; break;
        case EXTH_LANGUAGE: meta.language = text; break;
        // An EXTH title overwrites the record-0 title, because that is what
        // an editor writes when a title is changed.
        case EXTH_TITLE: meta.title = text; break;
        default: break; // unknown record type: ignored
        }
    }
}

Result<FileMetadata> parse(const QByteArray &bytes) {
    if (bytes.size() < PALM_HEADER_LEN + 8 || bytes.mid(60, 8) != QByteArrayLiteral("BOOKMOBI"))
        return Error::decode(QStringLiteral("not a MOBI file"));

    // Record 0 spans from its own offset to the next record's; each PalmDB
    // record entry is 8 bytes.
    const std::optional<quint16> recordCount = beU16(bytes, 76);
    if (!recordCount || *recordCount < 2)
        return Error::decode(QStringLiteral("MOBI has no content records"));

    const std::optional<quint32> start = beU32(bytes, PALM_HEADER_LEN);
    const std::optional<quint32> end = beU32(bytes, PALM_HEADER_LEN + 8);
    if (!start || !end || *end < *start || *end > bytes.size())
        return Error::decode(QStringLiteral("MOBI record 0 out of range"));
    const QByteArray record0 = bytes.mid(*start, *end - *start);

    if (record0.mid(PALMDOC_HEADER_LEN, 4) != QByteArrayLiteral("MOBI"))
        return Error::decode(QStringLiteral("MOBI header missing"));

    const std::optional<quint32> headerLenOpt = beU32(record0, PALMDOC_HEADER_LEN + 4);
    if (!headerLenOpt)
        return Error::decode(QStringLiteral("MOBI header truncated"));
    const quint32 headerLen = *headerLenOpt;

    const std::optional<quint32> encodingOpt = beU32(record0, PALMDOC_HEADER_LEN + 12);
    if (!encodingOpt)
        return Error::decode(QStringLiteral("MOBI header truncated"));
    const quint32 encoding = *encodingOpt;

    FileMetadata meta;

    // The full title sits in record 0 after the headers, located by offset.
    // EXTH 503 usually repeats it, but not always, so both are read and
    // EXTH wins below: it is what calibre and Kindle write when the title
    // is edited.
    const std::optional<quint32> titleOffset = beU32(record0, PALMDOC_HEADER_LEN + 84);
    const std::optional<quint32> titleLen = beU32(record0, PALMDOC_HEADER_LEN + 88);
    if (titleOffset && titleLen) {
        const qint64 titleStart = *titleOffset;
        const qint64 titleEnd = titleStart + static_cast<qint64>(*titleLen);
        if (titleStart >= 0 && titleEnd <= record0.size() && titleEnd >= titleStart)
            meta.title = clean(decodeText(record0.mid(titleStart, titleEnd - titleStart), encoding));
    }

    const quint32 flags = beU32(record0, PALMDOC_HEADER_LEN + 128).value_or(0);
    if (flags & EXTH_PRESENT) {
        const qint64 exthStart = PALMDOC_HEADER_LEN + static_cast<qint64>(headerLen);
        // A corrupt or hostile header_len can point past record 0 entirely;
        // that is simply "no EXTH block" rather than a crash.
        if (exthStart >= 0 && exthStart <= record0.size())
            readExth(record0.mid(exthStart), meta, encoding);
    }

    return Result<FileMetadata>::ok(meta);
}

} // namespace

Result<FileMetadata> readMetadata(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return Error::io(QStringLiteral("Could not open %1: %2").arg(path, file.errorString()));

    const QByteArray bytes = file.readAll();
    return parse(bytes);
}

} // namespace Mobi
