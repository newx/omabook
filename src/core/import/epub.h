// EPUB metadata and text extraction, ported from omabook-core/src/import/epub.rs.
//
// An EPUB is a zip: META-INF/container.xml points at an OPF package document,
// whose <metadata> carries Dublin Core fields and whose <manifest> names the
// cover image. No third-party EPUB library is needed for this, and the
// reader never comes through here -- it renders in foliate-js (SPEC §2.2).
#pragma once

#include "core/import/metadata.h"
#include "core/result.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QXmlStreamAttributes>
#include <QXmlStreamEntityResolver>
#include <QXmlStreamReader>

#include <optional>

namespace Epub {

// Full metadata: title, authors, description, cover, etc. Reads
// META-INF/container.xml, the OPF it points at, and the resolved cover
// image.
Result<FileMetadata> readMetadata(const QString &path);

// All the readable text of the EPUB, in spine order, with markup stripped.
// Used to build the retrieval corpus (SPEC §5.6). A single unreadable spine
// document is skipped, not fatal; only a whole-book text budget overrun is.
Result<QString> extractText(const QString &path);

// --- Pure logic below, testable on raw bytes without a zip file ----------
//
// These are declared `static` and defined right here so that both epub.cpp
// and the test binary's translation unit get their own working copy
// (CLAUDE.md, "Pure logic goes in static member functions" -- there is no
// natural class to hang these on, so a header-defined static free function
// plays the same role: reachable from a test with no application, no
// zip reader, and no file on disk).

// XML local name with any namespace prefix stripped ("dc:title" -> "title").
// QXmlStreamReader::name() already does this when namespace processing is
// on, but real OPFs sometimes use a prefix Qt refuses to bind (an
// undeclared "opf:"), so this is the fallback the OPF parsers below use
// alongside namespaceUri().
static QString localName(const QString &qualifiedName) {
    const int colon = qualifiedName.lastIndexOf(QLatin1Char(':'));
    return colon >= 0 ? qualifiedName.mid(colon + 1) : qualifiedName;
}

// Manifest hrefs are relative to the OPF's directory, and these are zip
// entry strings, not filesystem paths, so this is resolved by hand: split
// on '/', drop '.', let '..' pop the last kept segment, rejoin.
static QString resolveRelative(const QString &opfPath, const QString &href) {
    const int slash = opfPath.lastIndexOf(QLatin1Char('/'));
    const QString dir = slash >= 0 ? opfPath.left(slash) : QString();
    const QString joined = dir.isEmpty() ? href : dir + QLatin1Char('/') + href;

    QStringList parts;
    const QStringList segments = joined.split(QLatin1Char('/'));
    for (const QString &part : segments) {
        if (part.isEmpty() || part == QLatin1String("."))
            continue;
        if (part == QLatin1String("..")) {
            if (!parts.isEmpty())
                parts.removeLast();
            continue;
        }
        parts.append(part);
    }
    return parts.join(QLatin1Char('/'));
}

// Accepts an ISBN-13 only with exactly 13 ASCII digits and a hint: an
// "isbn" scheme (already lowercased by the caller), "isbn" appearing in the
// lowercased raw value, or digits starting "97". A 13-digit number with
// none of those is some other identifier scheme, not an ISBN.
static std::optional<QString> asIsbn13(const QString &value, const QString &scheme) {
    QString digits;
    for (const QChar &c : value) {
        if (c.isDigit())
            digits.append(c);
    }
    if (digits.size() != 13)
        return std::nullopt;

    const bool looksDeclared =
            scheme.contains(QLatin1String("isbn")) || value.toLower().contains(QLatin1String("isbn"));
    if (looksDeclared || digits.startsWith(QLatin1String("97")))
        return digits;
    return std::nullopt;
}

// META-INF/container.xml -> the OPF's zip-entry path, from the rootfile
// element's full-path attribute. No rootfile named is an error: this is
// not a usable EPUB.
static Result<QString> containerRootfile(const QByteArray &containerXml) {
    class SpaceEntityResolver : public QXmlStreamEntityResolver {
    public:
        QString resolveUndeclaredEntity(const QString &) override { return QStringLiteral(" "); }
    };
    SpaceEntityResolver resolver;

    QXmlStreamReader reader(containerXml);
    reader.setEntityResolver(&resolver);

    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement())
            continue;
        if (localName(reader.qualifiedName().toString()) != QLatin1String("rootfile"))
            continue;
        const QString fullPath = reader.attributes().value(QLatin1String("full-path")).toString();
        if (!fullPath.isEmpty())
            return Result<QString>::ok(fullPath);
    }

    if (reader.hasError())
        return Error::xml(QStringLiteral("container.xml is malformed: %1").arg(reader.errorString()));

    return Error::xml(QStringLiteral("container.xml names no rootfile; this is not a usable EPUB"));
}

// Parses the OPF <metadata> block. Singular fields (title, description,
// publisher, language, date) take the first value seen; creator and
// subject push every occurrence. Empty and whitespace-only values are
// skipped. identifier captures its scheme attribute (lowercased) before
// its text is read, since the scheme is what makes a 13-digit value an
// ISBN rather than a UUID.
static Result<FileMetadata> parseOpfMetadata(const QByteArray &opf) {
    static const QString kDcNamespace = QStringLiteral("http://purl.org/dc/elements/1.1/");

    class SpaceEntityResolver : public QXmlStreamEntityResolver {
    public:
        QString resolveUndeclaredEntity(const QString &) override { return QStringLiteral(" "); }
    };
    SpaceEntityResolver resolver;

    QXmlStreamReader reader(opf);
    reader.setEntityResolver(&resolver);

    FileMetadata meta;
    QString scheme;

    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement())
            continue;

        const bool isDc = reader.namespaceUri() == kDcNamespace;
        const QString name = reader.name().toString();

        if (isDc && name == QLatin1String("identifier"))
            scheme = reader.attributes().value(QLatin1String("scheme")).toString().toLower();

        // Not a Dublin Core field: let the plain readNext() loop above
        // descend into it naturally (it may be <metadata>, <manifest>, or
        // any other OPF container we do not otherwise care about).
        // skipCurrentElement() here would consume the whole subtree -- fatal
        // on the very first element, <package>, which contains everything.
        if (!isDc)
            continue;

        const QString value = reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
        if (value.isEmpty())
            continue;

        if (name == QLatin1String("title")) {
            if (meta.title.isEmpty())
                meta.title = value;
        } else if (name == QLatin1String("creator")) {
            meta.authors.append(value);
        } else if (name == QLatin1String("description")) {
            if (meta.description.isEmpty())
                meta.description = value;
        } else if (name == QLatin1String("publisher")) {
            if (meta.publisher.isEmpty())
                meta.publisher = value;
        } else if (name == QLatin1String("language")) {
            if (meta.language.isEmpty())
                meta.language = value;
        } else if (name == QLatin1String("subject")) {
            meta.subjects.append(value);
        } else if (name == QLatin1String("date")) {
            if (meta.publishedDate.isEmpty())
                meta.publishedDate = value;
        } else if (name == QLatin1String("identifier")) {
            if (meta.isbn13.isEmpty()) {
                const std::optional<QString> isbn = asIsbn13(value, scheme);
                if (isbn)
                    meta.isbn13 = *isbn;
            }
        }
    }

    if (reader.hasError())
        return Error::xml(QStringLiteral("package document is malformed: %1").arg(reader.errorString()));

    return Result<FileMetadata>::ok(meta);
}

// EPUB 3 marks the cover with properties="cover-image"; EPUB 2 uses a
// <meta name="cover" content="id"> pointing at a manifest item; failing
// both, a manifest item whose id or href says "cover" and is an image
// extension. Both EPUB 2 and 3 are in the wild, so all three are tried in
// that order.
static std::optional<QString> coverHref(const QByteArray &opf) {
    class SpaceEntityResolver : public QXmlStreamEntityResolver {
    public:
        QString resolveUndeclaredEntity(const QString &) override { return QStringLiteral(" "); }
    };
    SpaceEntityResolver resolver;

    QXmlStreamReader reader(opf);
    reader.setEntityResolver(&resolver);

    struct Item {
        QString id, href, properties;
    };
    QList<Item> items;
    QString coverId;

    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement())
            continue;

        const QString name = reader.name().toString();
        const QXmlStreamAttributes attrs = reader.attributes();
        if (name == QLatin1String("item")) {
            const QString id = attrs.value(QLatin1String("id")).toString();
            const QString href = attrs.value(QLatin1String("href")).toString();
            if (!id.isEmpty() && !href.isEmpty())
                items.append(Item{id, href, attrs.value(QLatin1String("properties")).toString()});
        } else if (name == QLatin1String("meta")) {
            if (attrs.value(QLatin1String("name")).toString() == QLatin1String("cover"))
                coverId = attrs.value(QLatin1String("content")).toString();
        }
    }

    for (const Item &item : items) {
        if (item.properties.contains(QLatin1String("cover-image")))
            return item.href;
    }

    if (!coverId.isEmpty()) {
        for (const Item &item : items) {
            if (item.id == coverId)
                return item.href;
        }
    }

    static const QStringList kImageExtensions = { QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                                    QStringLiteral("png"), QStringLiteral("gif"),
                                                    QStringLiteral("webp"), QStringLiteral("svg") };
    for (const Item &item : items) {
        const bool looksLikeCover =
                item.id.toLower().contains(QLatin1String("cover")) || item.href.toLower().contains(QLatin1String("cover"));
        if (!looksLikeCover)
            continue;
        const int dot = item.href.lastIndexOf(QLatin1Char('.'));
        const QString ext = dot >= 0 ? item.href.mid(dot + 1).toLower() : QString();
        if (kImageExtensions.contains(ext))
            return item.href;
    }

    return std::nullopt;
}

// Document hrefs in reading order: the spine's idrefs resolved through the
// manifest. Reading the manifest alone would include stylesheets and
// images; unresolvable idrefs are dropped.
static QStringList spineHrefs(const QByteArray &opf) {
    class SpaceEntityResolver : public QXmlStreamEntityResolver {
    public:
        QString resolveUndeclaredEntity(const QString &) override { return QStringLiteral(" "); }
    };
    SpaceEntityResolver resolver;

    QXmlStreamReader reader(opf);
    reader.setEntityResolver(&resolver);

    QList<QPair<QString, QString>> manifest;
    QStringList order;

    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement())
            continue;

        const QString name = reader.name().toString();
        const QXmlStreamAttributes attrs = reader.attributes();
        if (name == QLatin1String("item")) {
            const QString id = attrs.value(QLatin1String("id")).toString();
            const QString href = attrs.value(QLatin1String("href")).toString();
            if (!id.isEmpty() && !href.isEmpty())
                manifest.append({ id, href });
        } else if (name == QLatin1String("itemref")) {
            const QString idref = attrs.value(QLatin1String("idref")).toString();
            if (!idref.isEmpty())
                order.append(idref);
        }
    }

    QStringList hrefs;
    for (const QString &idref : order) {
        for (const auto &entry : manifest) {
            if (entry.first == idref) {
                hrefs.append(entry.second);
                break;
            }
        }
    }
    return hrefs;
}

// Text content of one XHTML document. <script> and <style> subtrees are
// dropped; everything else contributes its text. Block tags emit a
// newline on open (and close, except <br> which has no closing content).
static QString stripMarkup(const QByteArray &xhtml) {
    class SpaceEntityResolver : public QXmlStreamEntityResolver {
    public:
        QString resolveUndeclaredEntity(const QString &) override { return QStringLiteral(" "); }
    };
    SpaceEntityResolver resolver;

    QXmlStreamReader reader(xhtml);
    reader.setEntityResolver(&resolver);

    QString out;
    int skipDepth = 0;

    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QString name = reader.name().toString().toLower();
            if (name == QLatin1String("script") || name == QLatin1String("style")) {
                ++skipDepth;
            } else if (name == QLatin1String("p") || name == QLatin1String("div")
                       || name == QLatin1String("br") || name == QLatin1String("li")
                       || name == QLatin1String("h1") || name == QLatin1String("h2")
                       || name == QLatin1String("h3") || name == QLatin1String("h4")
                       || name == QLatin1String("h5") || name == QLatin1String("h6")) {
                out.append(QLatin1Char('\n'));
            }
        } else if (token == QXmlStreamReader::EndElement) {
            const QString name = reader.name().toString().toLower();
            if (name == QLatin1String("script") || name == QLatin1String("style")) {
                if (skipDepth > 0)
                    --skipDepth;
            } else if (name == QLatin1String("p") || name == QLatin1String("div")
                       || name == QLatin1String("li") || name == QLatin1String("h1")
                       || name == QLatin1String("h2") || name == QLatin1String("h3")
                       || name == QLatin1String("h4") || name == QLatin1String("h5")
                       || name == QLatin1String("h6")) {
                out.append(QStringLiteral("\n\n"));
            }
        } else if (token == QXmlStreamReader::Characters) {
            if (skipDepth == 0)
                out.append(reader.text());
        }
    }

    return out;
}

} // namespace Epub
