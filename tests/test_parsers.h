// Tests for the EPUB, MOBI and PDF parsers, ported from the Rust tests of
// the same intent in omabook-core/src/import/{epub,mobi,pdf}.rs.
#pragma once

#include <QtTest>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "core/import/epub.h"
#include "core/import/metadata.h"
#include "core/import/mobi.h"
#include "core/import/pdf.h"
#include "core/models/book.h"
#include "core/result.h"

namespace {

// Writes `bytes` to `name` under `dir` and returns the path. Used to hand
// synthetic MOBI files to Mobi::readMetadata, which -- like the rest of
// this codebase -- takes a path rather than a buffer.
QString writeTempFile(QTemporaryDir &dir, const QString &name, const QByteArray &bytes) {
    const QString path = dir.filePath(name);
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(bytes);
    file.close();
    return path;
}

void appendBe32(QByteArray &out, quint32 value) {
    out.append(char((value >> 24) & 0xFF));
    out.append(char((value >> 16) & 0xFF));
    out.append(char((value >> 8) & 0xFF));
    out.append(char(value & 0xFF));
}

void appendBe16(QByteArray &out, quint16 value) {
    out.append(char((value >> 8) & 0xFF));
    out.append(char(value & 0xFF));
}

void overwrite(QByteArray &out, qsizetype at, const QByteArray &value) {
    out.replace(at, value.size(), value);
}

// Builds the smallest MOBI file the parser accepts: a Palm header, two
// record entries, and a record 0 carrying a MOBI header, a full title, and
// the given EXTH records. Ported byte-for-byte from the Rust reference's
// synthetic_mobi() test helper.
QByteArray synthesizeMobi(const QString &title, const QList<QPair<quint32, QString>> &exth) {
    const QByteArray titleBytes = title.toUtf8();

    QByteArray exthBlock;
    for (const auto &record : exth) {
        appendBe32(exthBlock, record.first);
        const QByteArray value = record.second.toUtf8();
        appendBe32(exthBlock, quint32(8 + value.size()));
        exthBlock.append(value);
    }
    QByteArray exthFull = QByteArrayLiteral("EXTH");
    appendBe32(exthFull, quint32(12 + exthBlock.size()));
    appendBe32(exthFull, quint32(exth.size()));
    exthFull.append(exthBlock);

    constexpr quint32 headerLen = 232;
    constexpr quint32 EXTH_PRESENT = 0x40;
    QByteArray mobi(16 + int(headerLen), char(0));
    overwrite(mobi, 16, QByteArrayLiteral("MOBI"));
    QByteArray tmp;
    appendBe32(tmp, headerLen);
    overwrite(mobi, 20, tmp);
    tmp.clear();
    appendBe32(tmp, quint32(65001));
    overwrite(mobi, 28, tmp);

    const quint32 titleOffset = quint32(mobi.size() + exthFull.size());
    tmp.clear();
    appendBe32(tmp, titleOffset);
    overwrite(mobi, 100, tmp);
    tmp.clear();
    appendBe32(tmp, quint32(titleBytes.size()));
    overwrite(mobi, 104, tmp);
    tmp.clear();
    appendBe32(tmp, EXTH_PRESENT);
    overwrite(mobi, 144, tmp);

    mobi.append(exthFull);
    mobi.append(titleBytes);

    const quint32 record0Start = quint32(78 + 2 * 8);
    QByteArray file(78, char(0));
    overwrite(file, 60, QByteArrayLiteral("BOOKMOBI"));
    tmp.clear();
    appendBe16(tmp, quint16(2));
    overwrite(file, 76, tmp);
    appendBe32(file, record0Start);
    file.append(QByteArray(4, char(0)));
    appendBe32(file, quint32(record0Start + mobi.size()));
    file.append(QByteArray::fromHex("00000001"));
    file.append(mobi);
    file.append(QByteArrayLiteral("text"));
    return file;
}

// Shells out to /usr/bin/zip to build an EPUB, since Qt has no zip writer
// and QZipReader is read-only. `files` are zip-entry-path/content pairs.
bool buildEpub(const QString &epubPath, const QList<QPair<QString, QByteArray>> &files) {
    QTemporaryDir sourceDir;
    if (!sourceDir.isValid())
        return false;

    QStringList relativePaths;
    for (const auto &entry : files) {
        const QString fullPath = sourceDir.filePath(entry.first);
        if (!QDir().mkpath(QFileInfo(fullPath).absolutePath()))
            return false;
        QFile file(fullPath);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        file.write(entry.second);
        file.close();
        relativePaths.append(entry.first);
    }

    QProcess zip;
    zip.setWorkingDirectory(sourceDir.path());
    QStringList args = { QStringLiteral("-q"), epubPath };
    args.append(relativePaths);
    zip.start(QStringLiteral("/usr/bin/zip"), args);
    if (!zip.waitForStarted(10000))
        return false;
    if (!zip.waitForFinished(10000))
        return false;
    return zip.exitStatus() == QProcess::NormalExit && zip.exitCode() == 0;
}

} // namespace

class ParsersTest : public QObject {
    Q_OBJECT

private slots:
    // --- EPUB: pure logic on raw bytes, no zip needed -------------------

    void stripsNamespacePrefixes() {
        QCOMPARE(Epub::localName(QStringLiteral("dc:title")), QStringLiteral("title"));
        QCOMPARE(Epub::localName(QStringLiteral("title")), QStringLiteral("title"));
    }

    void resolvesHrefsAgainstTheOpfDirectory() {
        QCOMPARE(Epub::resolveRelative(QStringLiteral("EPUB/package.opf"), QStringLiteral("images/cover.jpg")),
                 QStringLiteral("EPUB/images/cover.jpg"));
        QCOMPARE(Epub::resolveRelative(QStringLiteral("package.opf"), QStringLiteral("cover.jpg")),
                 QStringLiteral("cover.jpg"));
        QCOMPARE(Epub::resolveRelative(QStringLiteral("OEBPS/content.opf"), QStringLiteral("../images/c.png")),
                 QStringLiteral("images/c.png"));
    }

    void isbnNeedsThirteenDigitsAndAHint() {
        const std::optional<QString> a = Epub::asIsbn13(QStringLiteral("urn:isbn:9780306406157"), QString());
        QVERIFY(a.has_value());
        QCOMPARE(*a, QStringLiteral("9780306406157"));

        const std::optional<QString> b = Epub::asIsbn13(QStringLiteral("9780306406157"), QStringLiteral("isbn"));
        QVERIFY(b.has_value());
        QCOMPARE(*b, QStringLiteral("9780306406157"));

        QVERIFY(!Epub::asIsbn13(QStringLiteral("uuid-1234"), QString()).has_value());
        // A 13-digit number with no ISBN hint and no 97 prefix is not an ISBN.
        QVERIFY(!Epub::asIsbn13(QStringLiteral("1234567890123"), QString()).has_value());
    }

    void parsesDublinCoreMetadata() {
        const QByteArray opf = QByteArrayLiteral(
                "<package xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
                "<metadata>"
                "<dc:title>Moby-Dick</dc:title>"
                "<dc:creator>Herman Melville</dc:creator>"
                "<dc:creator>An Editor</dc:creator>"
                "<dc:language>en-US</dc:language>"
                "<dc:publisher>Harper</dc:publisher>"
                "</metadata>"
                "</package>");
        const Result<FileMetadata> result = Epub::parseOpfMetadata(opf);
        QVERIFY(result.isOk());
        const FileMetadata meta = result.value();
        QCOMPARE(meta.title, QStringLiteral("Moby-Dick"));
        QCOMPARE(meta.authors, QStringList({ QStringLiteral("Herman Melville"), QStringLiteral("An Editor") }));
        QCOMPARE(meta.language, QStringLiteral("en-US"));
        QCOMPARE(meta.publisher, QStringLiteral("Harper"));
    }

    void collectsSubjectsAsTags() {
        const QByteArray opf = QByteArrayLiteral(
                "<package xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><metadata>"
                "<dc:subject>Whaling -- Fiction</dc:subject>"
                "<dc:subject>Sea stories</dc:subject>"
                "</metadata></package>");
        const Result<FileMetadata> result = Epub::parseOpfMetadata(opf);
        QVERIFY(result.isOk());
        QCOMPARE(result.value().subjects,
                 QStringList({ QStringLiteral("Whaling -- Fiction"), QStringLiteral("Sea stories") }));
    }

    void findsTheEpub3CoverProperty() {
        const QByteArray opf = QByteArrayLiteral(
                "<package><manifest>"
                "<item id=\"x\" href=\"text/ch1.xhtml\" media-type=\"application/xhtml+xml\"/>"
                "<item id=\"c\" href=\"images/cover.jpg\" properties=\"cover-image\"/>"
                "</manifest></package>");
        const std::optional<QString> href = Epub::coverHref(opf);
        QVERIFY(href.has_value());
        QCOMPARE(*href, QStringLiteral("images/cover.jpg"));
    }

    void fallsBackToTheEpub2MetaCover() {
        const QByteArray opf = QByteArrayLiteral(
                "<package>"
                "<metadata><meta name=\"cover\" content=\"cid\"/></metadata>"
                "<manifest><item id=\"cid\" href=\"img/front.png\"/></manifest>"
                "</package>");
        const std::optional<QString> href = Epub::coverHref(opf);
        QVERIFY(href.has_value());
        QCOMPARE(*href, QStringLiteral("img/front.png"));
    }

    void undeclaredEntitiesDoNotAbortTheParse() {
        const QByteArray opf = QByteArrayLiteral(
                "<package xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><metadata>"
                "<dc:title>Weird&nbsp;Title</dc:title>"
                "</metadata></package>");
        const Result<FileMetadata> result = Epub::parseOpfMetadata(opf);
        QVERIFY(result.isOk());
        QCOMPARE(result.value().title, QStringLiteral("Weird Title"));
    }

    void titleFallsBackToATidiedFilename() {
        FileMetadata meta;
        const QString path = QStringLiteral("/books/childrens-literature.epub");
        QCOMPARE(meta.titleOrFilename(path), QStringLiteral("childrens literature"));
    }

    // --- EPUB: real zip I/O ----------------------------------------------

    void rejectsAZipEntryThatInflatesPastTheCap() {
        QTemporaryDir outDir;
        QVERIFY(outDir.isValid());
        const QString epubPath = outDir.filePath(QStringLiteral("bomb.epub"));

        const QByteArray container = QByteArrayLiteral(
                "<?xml version=\"1.0\"?>"
                "<container xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\" version=\"1.0\">"
                "<rootfiles><rootfile full-path=\"content.opf\" "
                "media-type=\"application/oebps-package+xml\"/></rootfiles>"
                "</container>");

        // A single entry that decompresses to more than the 32 MiB
        // per-entry cap. It is a run of one repeated byte, so DEFLATE
        // shrinks it to a few KB on disk while the declared/inflated size
        // is well over the cap -- the shape of a zip bomb. The declared
        // size is honestly reported here (a real zip writer does not lie
        // about it); the point of the cap is that the reader must never
        // skip checking it, not that this particular writer is hostile.
        const QByteArray bomb(33 * 1024 * 1024, 'a');

        QVERIFY(buildEpub(epubPath, { { QStringLiteral("META-INF/container.xml"), container },
                                       { QStringLiteral("content.opf"), bomb } }));

        const Result<FileMetadata> result = Epub::readMetadata(epubPath);
        QVERIFY(result.isErr());
    }

    void endToEndAgainstARealEpubIfPresent() {
        const QString path = QDir::homePath() + QStringLiteral("/Books/Poetry/wasteland.epub");
        if (!QFileInfo::exists(path))
            QSKIP("no local EPUB fixture at ~/Books/Poetry/wasteland.epub");

        const Result<FileMetadata> meta = Epub::readMetadata(path);
        QVERIFY(meta.isOk());
        QVERIFY(!meta.value().titleOrFilename(path).isEmpty());

        const Result<QString> text = Epub::extractText(path);
        QVERIFY(text.isOk());
        QVERIFY(!text.value().trimmed().isEmpty());
    }

    // --- MOBI --------------------------------------------------------------

    void readsTheExthRecordsThatMatter() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray bytes = synthesizeMobi(
                QStringLiteral("Full Name"),
                { { 100, QStringLiteral("Ursula K. Le Guin") }, // AUTHOR
                  { 105, QStringLiteral("FICTION / Science Fiction / General") }, // SUBJECT
                  { 105, QStringLiteral("Anarchism -- Fiction") },
                  { 104, QStringLiteral("9780061054884") }, // ISBN
                  { 101, QStringLiteral("Harper") }, // PUBLISHER
                  { 503, QStringLiteral("The Dispossessed") }, // TITLE
                  { 999, QStringLiteral("ignored") } });
        const QString path = writeTempFile(dir, QStringLiteral("book.mobi"), bytes);

        const Result<FileMetadata> result = Mobi::readMetadata(path);
        QVERIFY(result.isOk());
        const FileMetadata meta = result.value();
        QCOMPARE(meta.title, QStringLiteral("The Dispossessed")); // EXTH title wins
        QCOMPARE(meta.authors, QStringList({ QStringLiteral("Ursula K. Le Guin") }));
        QCOMPARE(meta.subjects.size(), 2);
        QCOMPARE(meta.isbn13, QStringLiteral("9780061054884"));
        QCOMPARE(meta.publisher, QStringLiteral("Harper"));
    }

    void theFullNameIsTheTitleWhenExthHasNone() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray bytes = synthesizeMobi(QStringLiteral("Full Name"), {});
        const QString path = writeTempFile(dir, QStringLiteral("book.mobi"), bytes);

        const Result<FileMetadata> result = Mobi::readMetadata(path);
        QVERIFY(result.isOk());
        QCOMPARE(result.value().title, QStringLiteral("Full Name"));
        QVERIFY(result.value().authors.isEmpty());
    }

    void aFileThatIsNotMobiIsRefusedNotMisread() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString zipLike =
                writeTempFile(dir, QStringLiteral("fake.mobi"), QByteArrayLiteral("PK\x03\x04 this is a zip"));
        QVERIFY(Mobi::readMetadata(zipLike).isErr());

        const QString empty = writeTempFile(dir, QStringLiteral("empty.mobi"), QByteArray());
        QVERIFY(Mobi::readMetadata(empty).isErr());
    }

    void aCorruptExthLengthCannotLoop() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QByteArray bytes = synthesizeMobi(QStringLiteral("T"), { { 100, QStringLiteral("A") } });

        // Zero the length of the first EXTH record: EXTH header (4) +
        // total length (4) + count (4) + this record's kind (4) is where
        // its length field starts.
        const qsizetype exthPos = bytes.indexOf(QByteArrayLiteral("EXTH"));
        QVERIFY(exthPos >= 0);
        const qsizetype lenFieldPos = exthPos + 12 + 4;
        bytes.replace(lenFieldPos, 4, QByteArray(4, char(0)));

        const QString path = writeTempFile(dir, QStringLiteral("corrupt.mobi"), bytes);
        const Result<FileMetadata> result = Mobi::readMetadata(path);
        QVERIFY(result.isOk());
        QVERIFY(result.value().authors.isEmpty());
    }

    // --- PDF: pure logic, no PDF file needed ------------------------------

    void pagesWithNoSharedLinesAreNotNoisy() {
        const QStringList texts = { QStringLiteral("alpha beta\ngamma delta"),
                                     QStringLiteral("epsilon zeta\neta theta") };
        QCOMPARE(Pdf::repeatedLineRatio(texts), 0.0);
    }

    void runningHeadersShowUpAsRepetition() {
        const QStringList texts = { QStringLiteral("CHAPTER ONE\nreal content here"),
                                     QStringLiteral("CHAPTER ONE\ndifferent content") };
        QVERIFY(Pdf::repeatedLineRatio(texts) > 0.4);
    }

    void aSingleSampleCannotShowRepetition() {
        QCOMPARE(Pdf::repeatedLineRatio({ QStringLiteral("only one page") }), 0.0);
    }

    void ordinaryProseIsNotGibberish() {
        const QString text =
                QStringLiteral("Call me Ishmael. Some years ago, never mind how long precisely, "
                                "having little or no money in my purse, I thought I would sail about.");
        QVERIFY(!Pdf::looksLikeGibberish(text));
    }

    void mathematicalProseIsNotGibberish() {
        // Symbols are fine when they sit between words rather than inside them.
        const QString text =
                QStringLiteral("Theorem 3.1. For every x in the set A, we have x = y + 2 and "
                                "the union A ∪ B is closed under addition, so f(x) ≤ g(x) holds.");
        QVERIFY(!Pdf::looksLikeGibberish(text));
    }

    void symbolFontMojibakeIsGibberish() {
        // A real page from a PDF whose text is in an unmapped symbol font.
        const QString text = QStringLiteral(
                "páÇÉë=çÑ=~=é~ê~ääÉäçÖê~ãW=~I=Ä= aá~Öçå~äëW= ÇN=~åÇ=ÇO= "
                "o = = O = 203. i = O(~ + Ä) = = 204. hÉóW=ï=Z=ïáÇíÜ");
        QVERIFY(Pdf::looksLikeGibberish(text));
    }

    void aShortFragmentIsNotJudged() {
        // Too little text to tell; other checks handle emptiness.
        QVERIFY(!Pdf::looksLikeGibberish(QStringLiteral("A = B")));
    }

    void aMissingFileIsReportedAsUnreadableNotAPanic() {
        QCOMPARE(Pdf::assessTextQuality(QStringLiteral("/nonexistent/file.pdf")), TextQuality::None_);
    }
};
