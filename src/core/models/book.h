// The Book aggregate and its small enumerations. Plain data types: no
// persistence, no I/O -- see src/core/repo for those.
#pragma once

#include "core/result.h"

#include <QMetaObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <optional>

// Q_ENUM needs a Q_OBJECT/Q_GADGET/Q_NAMESPACE context to register the enum
// with the meta-object system; there is no natural class to hang these
// three on, so they get a namespace of their own. Book, below, pulls the
// enum names into the global namespace with `using` so call sites read
// `BookStatus::Reading` rather than `BookEnums::BookStatus::Reading`.
namespace BookEnums {
Q_NAMESPACE

// Where a book sits in the reading lifecycle.
enum class BookStatus { Unread, Reading, Finished, Abandoned };
Q_ENUM_NS(BookStatus)

// How faithfully text could be extracted, which gates the AI features. See
// SPEC §7.2 -- this is set by the import pipeline, never guessed at read
// time. Named TextQuality::None_ rather than ::None because `None` collides
// with macros on some platforms; the wire encoding is still exactly "none".
enum class TextQuality { Good, Poor, None_ };
Q_ENUM_NS(TextQuality)

// The file format as found on disk. After import, Book::readablePath()
// always points at EPUB or PDF -- see SPEC §5.2.
enum class BookFormat { Epub, Pdf, Mobi, Azw3, Cbz };
Q_ENUM_NS(BookFormat)

} // namespace BookEnums

using BookEnums::BookFormat;
using BookEnums::BookStatus;
using BookEnums::TextQuality;

QString toString(BookStatus status);
QString toString(TextQuality quality);
QString toString(BookFormat format);

// Result<T> fromString(const QString &), one instantiation per enum, chosen
// with an explicit template argument: fromString<BookStatus>(text). An
// unrecognised string is an error, never a default -- silently defaulting
// would hide a corrupt row or a schema drift instead of surfacing it.
template <typename T>
Result<T> fromString(const QString &text);

template <>
Result<BookStatus> fromString<BookStatus>(const QString &text);
template <>
Result<TextQuality> fromString<TextQuality>(const QString &text);
template <>
Result<BookFormat> fromString<BookFormat>(const QString &text);

// Whether extracting this format's text needs an external converter.
//
// Not about reading: foliate-js opens MOBI and AZW3 directly, so these
// import and read with nothing else installed. Calibre only helps build the
// retrieval corpus for them, which is why it stays optional.
bool needsConversion(BookFormat format);

// Case-insensitive, tolerates a leading dot ("epub", ".EPUB", "Epub" all
// match). Returns nullopt for anything unrecognised.
std::optional<BookFormat> bookFormatFromExtension(const QString &extension);

// RFC3339 UTC -- used for Book::addedAt and anywhere else a stored timestamp needs a fixed format.
QString nowIso8601();

// A book as stored. Identity is `id`; `fileHash` is what makes import
// idempotent across re-scans.
//
// Several optional fields (subtitle, readingPath, coverPath, description,
// categoryId) are flattened to a plain QString/qint64 with an empty-string or
// zero sentinel, since that is
// how they round-trip through QSqlQuery::value() and QVariant in the repo
// layer; textQuality keeps std::optional because "no quality assessed yet"
// and "quality is none" are different states that must not collapse.
struct Book {
    qint64 id = 0;
    QString title;
    QString subtitle;
    QStringList authors;
    QString sourcePath;
    QString readingPath;
    BookFormat format = BookFormat::Epub;
    QString fileHash;
    qint64 fileSize = 0;
    QString coverPath;
    QString description;
    qint64 categoryId = 0; // 0 = none
    bool isFavorite = false;
    BookStatus status = BookStatus::Unread;
    std::optional<TextQuality> textQuality;
    QString addedAt;
    double progress = 0.0; // 0.0-1.0, joined from reading_progress; 0.0 when never opened.

    // What the reader should open: the converted file when one exists,
    // otherwise the original.
    QString readablePath() const { return readingPath.isEmpty() ? sourcePath : readingPath; }

    QString authorLine() const {
        return authors.isEmpty() ? QStringLiteral("Unknown author") : authors.join(QStringLiteral(", "));
    }

    // The books.authors column is a JSON array stored as text, e.g.
    // ["Herman Melville"]. Static so tests can round-trip them without a
    // database (CLAUDE.md, "Pure logic goes in static member functions").
    static QString encodeAuthors(const QStringList &authors);
    static QStringList decodeAuthors(const QString &json);
};

// The fields required to create a book. Separate from Book so that
// constructing one cannot invent an id or a progress.
struct NewBook {
    QString title;
    QStringList authors;
    QString sourcePath;
    BookFormat format = BookFormat::Epub;
    QString fileHash;
    qint64 fileSize = 0;
    QString description;
    QString publisher;
    QString language;
    QString publishedDate;
    QString isbn13;
    QString coverPath;
    std::optional<TextQuality> textQuality;
    std::optional<qint64> pageCount;
};
