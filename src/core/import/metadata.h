// The shared types every parser produces, ported from
// omabook-core/src/import/metadata.rs. Written first, and kept exactly to
// this shape, because the EPUB, MOBI and PDF parsers are all built against
// it.
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <optional>

// Raw bytes of a cover image extracted from a book file, plus the
// extension to store it under.
struct Cover {
    QByteArray bytes;
    QString extension; // lowercase, no dot
};

// What the import pipeline learns about a book from the file itself,
// before any online provider is consulted.
struct FileMetadata {
    QString title;
    QString description;
    QString publisher;
    QString language;
    QString publishedDate;
    QString isbn13;
    QStringList authors;
    QStringList subjects; // dc:subject entries, which become tags
    std::optional<Cover> cover;

    // A title to fall back on when the file declares none, or declares
    // rubbish: the filename, tidied. Better than showing a user "untitled"
    // -- or a publisher's internal identifier.
    QString titleOrFilename(const QString &path) const;

    // PDF `Title` fields are frequently the typesetter's or publisher's
    // internal identifier rather than the book's name -- "PII:
    // B978-0-12-421180-3.50000-0", "Microsoft Word - final.doc",
    // "untitled". The filename beats all of those. Static so tests can
    // exercise it without constructing a FileMetadata (CLAUDE.md, "Pure
    // logic goes in static member functions").
    static bool isJunkTitle(const QString &title);
};
