#include "book.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

namespace {

// Rust's from_extension trims a single leading '.' before lowercasing;
// QString::trimmed() trims whitespace, not punctuation, so that step is
// spelled out here.
QString withoutLeadingDot(const QString &text) {
    if (text.startsWith(QLatin1Char('.')))
        return text.mid(1);
    return text;
}

} // namespace

QString toString(BookStatus status) {
    switch (status) {
    case BookStatus::Unread:
        return QStringLiteral("unread");
    case BookStatus::Reading:
        return QStringLiteral("reading");
    case BookStatus::Finished:
        return QStringLiteral("finished");
    case BookStatus::Abandoned:
        return QStringLiteral("abandoned");
    }
    Q_UNREACHABLE_RETURN(QString());
}

QString toString(TextQuality quality) {
    switch (quality) {
    case TextQuality::Good:
        return QStringLiteral("good");
    case TextQuality::Poor:
        return QStringLiteral("poor");
    case TextQuality::None_:
        return QStringLiteral("none");
    }
    Q_UNREACHABLE_RETURN(QString());
}

QString toString(BookFormat format) {
    switch (format) {
    case BookFormat::Epub:
        return QStringLiteral("epub");
    case BookFormat::Pdf:
        return QStringLiteral("pdf");
    case BookFormat::Mobi:
        return QStringLiteral("mobi");
    case BookFormat::Azw3:
        return QStringLiteral("azw3");
    case BookFormat::Cbz:
        return QStringLiteral("cbz");
    }
    Q_UNREACHABLE_RETURN(QString());
}

template <>
Result<BookStatus> fromString<BookStatus>(const QString &text) {
    if (text == QStringLiteral("unread"))
        return Result<BookStatus>::ok(BookStatus::Unread);
    if (text == QStringLiteral("reading"))
        return Result<BookStatus>::ok(BookStatus::Reading);
    if (text == QStringLiteral("finished"))
        return Result<BookStatus>::ok(BookStatus::Finished);
    if (text == QStringLiteral("abandoned"))
        return Result<BookStatus>::ok(BookStatus::Abandoned);
    return Error::decode(QStringLiteral("%1 is not a valid BookStatus").arg(text));
}

template <>
Result<TextQuality> fromString<TextQuality>(const QString &text) {
    if (text == QStringLiteral("good"))
        return Result<TextQuality>::ok(TextQuality::Good);
    if (text == QStringLiteral("poor"))
        return Result<TextQuality>::ok(TextQuality::Poor);
    if (text == QStringLiteral("none"))
        return Result<TextQuality>::ok(TextQuality::None_);
    return Error::decode(QStringLiteral("%1 is not a valid TextQuality").arg(text));
}

template <>
Result<BookFormat> fromString<BookFormat>(const QString &text) {
    if (text == QStringLiteral("epub"))
        return Result<BookFormat>::ok(BookFormat::Epub);
    if (text == QStringLiteral("pdf"))
        return Result<BookFormat>::ok(BookFormat::Pdf);
    if (text == QStringLiteral("mobi"))
        return Result<BookFormat>::ok(BookFormat::Mobi);
    if (text == QStringLiteral("azw3"))
        return Result<BookFormat>::ok(BookFormat::Azw3);
    if (text == QStringLiteral("cbz"))
        return Result<BookFormat>::ok(BookFormat::Cbz);
    return Error::decode(QStringLiteral("%1 is not a valid BookFormat").arg(text));
}

bool needsConversion(BookFormat format) {
    return format == BookFormat::Mobi || format == BookFormat::Azw3;
}

std::optional<BookFormat> bookFormatFromExtension(const QString &extension) {
    const QString normalized = withoutLeadingDot(extension).toLower();
    if (normalized == QStringLiteral("epub"))
        return BookFormat::Epub;
    if (normalized == QStringLiteral("pdf"))
        return BookFormat::Pdf;
    if (normalized == QStringLiteral("mobi"))
        return BookFormat::Mobi;
    if (normalized == QStringLiteral("azw3"))
        return BookFormat::Azw3;
    if (normalized == QStringLiteral("cbz"))
        return BookFormat::Cbz;
    return std::nullopt;
}

QString nowIso8601() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

QString Book::encodeAuthors(const QStringList &authors) {
    QJsonArray array;
    for (const QString &author : authors)
        array.append(author);
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QStringList Book::decodeAuthors(const QString &json) {
    QStringList authors;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isArray())
        return authors;

    const QJsonArray array = doc.array();
    for (const QJsonValue &value : array) {
        if (value.isString())
            authors << value.toString();
    }
    return authors;
}
