#pragma once

#include <QtTest>

#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVector>

#include "core/import/classify.h"
#include "core/import/covers.h"
#include "core/import/hash.h"
#include "core/import/metadata.h"
#include "core/import/scanner.h"
#include "core/result.h"

namespace {

// Writes `content` at `path`, creating any missing parent directories --
// the fixture writer every scanner/hash/covers test needs.
void writeFile(const QString &path, const QByteArray &content) {
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(content);
}

FileMetadata withSubjects(const QStringList &subjects) {
    FileMetadata meta;
    meta.subjects = subjects;
    return meta;
}

// store() reads Db::dataDir(), which is built directly from $XDG_DATA_HOME
// (CLAUDE.md, "Database"), not QStandardPaths -- so a covers test fakes it
// this way rather than with QStandardPaths::setTestModeEnabled. Restores
// the previous value on destruction, mirroring omawrite's HomeRestorer.
class XdgDataHomeRestorer {
public:
    explicit XdgDataHomeRestorer(const QString &path) : m_previous(qEnvironmentVariable("XDG_DATA_HOME")) {
        qputenv("XDG_DATA_HOME", path.toUtf8());
    }

    ~XdgDataHomeRestorer() {
        if (m_previous.isEmpty())
            qunsetenv("XDG_DATA_HOME");
        else
            qputenv("XDG_DATA_HOME", m_previous.toUtf8());
    }

    XdgDataHomeRestorer(const XdgDataHomeRestorer &) = delete;
    XdgDataHomeRestorer &operator=(const XdgDataHomeRestorer &) = delete;

private:
    QString m_previous;
};

} // namespace

class ImportTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    // --- import/scanner ------------------------------------------------

    void findsBooksAndIgnoresEverythingElse() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir.filePath(QStringLiteral("a.epub")), QByteArray(2048, 'x'));
        writeFile(dir.filePath(QStringLiteral("b.pdf")), QByteArray(2048, 'x'));
        writeFile(dir.filePath(QStringLiteral("notes.txt")), QByteArray(2048, 'x'));
        writeFile(dir.filePath(QStringLiteral("cover.jpg")), QByteArray(2048, 'x'));

        const QVector<Candidate> found = scan(dir.path());
        QCOMPARE(found.size(), 2);
        QCOMPARE(QFileInfo(found.at(0).path).fileName(), QStringLiteral("a.epub"));
        QCOMPARE(QFileInfo(found.at(1).path).fileName(), QStringLiteral("b.pdf"));
    }

    void recursesIntoSubdirectories() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir.filePath(QStringLiteral("fiction/scifi/dune.epub")), QByteArray(2048, 'x'));

        QCOMPARE(scan(dir.path()).size(), 1);
    }

    void skipsHiddenFilesAndTruncatedDownloads() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir.filePath(QStringLiteral(".hidden.epub")), QByteArray(2048, 'x'));
        writeFile(dir.filePath(QStringLiteral(".cache/x.epub")), QByteArray(2048, 'x'));
        writeFile(dir.filePath(QStringLiteral("stub.epub")), QByteArray(10, 'x'));

        QVERIFY(scan(dir.path()).isEmpty());
    }

    void aMissingRootYieldsNothingRatherThanFailing() {
        QVERIFY(scan(QStringLiteral("/nonexistent/library")).isEmpty());
    }

    // --- import/hash -----------------------------------------------------

    void identicalContentsHashAlikeRegardlessOfName() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString a = dir.filePath(QStringLiteral("a.epub"));
        const QString b = dir.filePath(QStringLiteral("b.epub"));
        writeFile(a, QByteArrayLiteral("the same bytes"));
        writeFile(b, QByteArrayLiteral("the same bytes"));

        const Result<QString> hashA = hashFile(a);
        const Result<QString> hashB = hashFile(b);
        QVERIFY(hashA.isOk());
        QVERIFY(hashB.isOk());
        QCOMPARE(hashA.value(), hashB.value());
    }

    void differentContentsDiffer() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString a = dir.filePath(QStringLiteral("c.epub"));
        const QString b = dir.filePath(QStringLiteral("d.epub"));
        writeFile(a, QByteArrayLiteral("one"));
        writeFile(b, QByteArrayLiteral("two"));

        QVERIFY(hashFile(a).value() != hashFile(b).value());
    }

    void samePrefixButDifferentLengthDiffers() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString a = dir.filePath(QStringLiteral("e.epub"));
        const QString b = dir.filePath(QStringLiteral("f.epub"));
        writeFile(a, QByteArrayLiteral("prefix"));
        writeFile(b, QByteArrayLiteral("prefix-and-more"));

        // Length is mixed in precisely so truncation is not invisible.
        QVERIFY(hashFile(a).value() != hashFile(b).value());
    }

    void missingFileIsAnErrorNotAPanic() {
        QVERIFY(hashFile(QStringLiteral("/nonexistent/nope.epub")).isErr());
    }

    // --- import/metadata ---------------------------------------------------

    void realTitlesAreKept() {
        const QStringList titles = {
            QStringLiteral("Moby-Dick"),
            QStringLiteral("Discrete Mathematics with Applications"),
            QStringLiteral("Calculus, 3ra Edición"),
            QStringLiteral("1300 Math Formulas"),
        };
        for (const QString &title : titles)
            QVERIFY2(!FileMetadata::isJunkTitle(title), qPrintable(title));
    }

    void publisherIdentifiersAreRejected() {
        const QStringList titles = {
            QStringLiteral("PII: B978-0-12-421180-3.50000-0"),
            QStringLiteral("The Project Gutenberg eBook #32625: A treatise on probability"),
            QStringLiteral("Microsoft Word - final_draft.doc"),
            QStringLiteral("untitled"),
            QStringLiteral("book.pdf"),
            QStringLiteral("978-0-12-421180-3"),
            QStringLiteral("x"),
        };
        for (const QString &title : titles)
            QVERIFY2(FileMetadata::isJunkTitle(title), qPrintable(title));
    }

    void aJunkTitleFallsBackToTheFilename() {
        FileMetadata meta;
        meta.title = QStringLiteral("PII: B978-0-12-421180-3.50000-0");
        QCOMPARE(meta.titleOrFilename(QStringLiteral("/books/Introduction_to_Topology.pdf")),
            QStringLiteral("Introduction to Topology"));
    }

    // --- import/covers -----------------------------------------------------

    void extensionsAreStrippedOfAnythingSurprising() {
        QCOMPARE(sanitiseExtension(QStringLiteral("jpg")), QStringLiteral("jpg"));
        QCOMPARE(sanitiseExtension(QStringLiteral("JPEG")), QStringLiteral("jpeg"));
        QCOMPARE(sanitiseExtension(QStringLiteral("../../etc/passwd")), QStringLiteral("etcpa"));
        QCOMPARE(sanitiseExtension(QString()), QStringLiteral("jpg"));
        QCOMPARE(sanitiseExtension(QStringLiteral("!!!")), QStringLiteral("jpg"));
    }

    void fileUrlsAreAbsolute() {
        const QString url = toFileUrl(QStringLiteral("/home/u/.local/share/omabook/covers/a.jpg"));
        QVERIFY(url.startsWith(QStringLiteral("file:///home/")));
    }

    void coversAreThumbnailed() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        XdgDataHomeRestorer xdgGuard(dir.path());

        const QSize original(1600, 1200);
        QImage image(original, QImage::Format_RGB32);
        image.fill(Qt::darkBlue);

        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "PNG"));
        buffer.close();

        const Cover cover{png, QStringLiteral("png")};
        const Result<QString> stored = store(QStringLiteral("abc123"), cover);
        QVERIFY(stored.isOk());

        const QImage written(stored.value());
        QVERIFY(!written.isNull());
        QVERIFY(written.width() <= 320);
        QVERIFY(written.height() <= 480);

        const double originalRatio = static_cast<double>(original.width()) / original.height();
        const double writtenRatio = static_cast<double>(written.width()) / written.height();
        QVERIFY(qAbs(originalRatio - writtenRatio) < 0.01);
    }

    // --- import/classify ---------------------------------------------------

    void aFolderBeatsEverything() {
        const FileMetadata meta = withSubjects({QStringLiteral("FICTION / Science Fiction")});
        QCOMPARE(categoryFor(QStringLiteral("Beach Reads"), meta).value(), QStringLiteral("Beach Reads"));
        // Blank folder is no folder.
        QCOMPARE(categoryFor(QStringLiteral("  "), meta).value(), QStringLiteral("Science Fiction"));
    }

    void bisacHeadsLandOnAShelf() {
        QCOMPARE(categoryFor(QString(), withSubjects({QStringLiteral("FICTION / Science Fiction / General")}))
                     .value(),
            QStringLiteral("Science Fiction"));
        QCOMPARE(
            categoryFor(QString(), withSubjects({QStringLiteral("COMPUTERS / Programming / Rust")})).value(),
            QStringLiteral("Programming"));
        QCOMPARE(categoryFor(QString(), withSubjects({QStringLiteral("JUVENILE FICTION / Animals")})).value(),
            QStringLiteral("Children"));
    }

    void libraryOfCongressHeadsLandOnAShelf() {
        QCOMPARE(
            categoryFor(QString(), withSubjects({QStringLiteral("Astronomy -- Early works to 1800")})).value(),
            QStringLiteral("Science"));
        QCOMPARE(categoryFor(QString(), withSubjects({QStringLiteral("Mathematics -- Philosophy")})).value(),
            QStringLiteral("Mathematics"));
        QCOMPARE(
            categoryFor(QString(), withSubjects({QStringLiteral("Children -- Books and reading")})).value(),
            QStringLiteral("Children"));
    }

    void theSpecificShelfWinsOverTheBroadOne() {
        // "Science Fiction" contains both "science" and "fiction".
        QCOMPARE(categoryFor(QString(), withSubjects({QStringLiteral("Science fiction")})).value(),
            QStringLiteral("Science Fiction"));
        // Mathematics is listed before Science, so "Science & Math" is Mathematics.
        QCOMPARE(categoryFor(QString(), withSubjects({QStringLiteral("Science & Math")})).value(),
            QStringLiteral("Mathematics"));
    }

    void aLaterSubjectCanNameTheShelfWhenTheFirstDoesNot() {
        const FileMetadata meta = withSubjects({
            QStringLiteral("Babbage, Charles, 1791-1871"),
            QStringLiteral("Mathematicians -- Great Britain -- Biography"),
        });
        // Neither head is a shelf word; the tail of the second is.
        QCOMPARE(categoryFor(QString(), meta).value(), QStringLiteral("Biography"));
    }

    void anUnknownHeadPassesThroughTitleCased() {
        QCOMPARE(categoryFor(QString(), withSubjects({QStringLiteral("PERPETUAL MOTION")})).value(),
            QStringLiteral("Perpetual Motion"));
    }

    void catalogueNoiseIsNotACategory() {
        // A name with dates, and an ISBN: neither can stand as a shelf.
        const FileMetadata meta = withSubjects({
            QStringLiteral("Babbage, Charles, 1791-1871"),
            QStringLiteral("9780061054884"),
        });
        QVERIFY(!categoryFor(QString(), meta).has_value());
    }

    void theTitleIsEnoughWhenThereAreNoSubjects() {
        FileMetadata meta;
        meta.title = QStringLiteral("Introduction to Linear Algebra");
        QCOMPARE(categoryFor(QString(), meta).value(), QStringLiteral("Mathematics"));

        FileMetadata other;
        other.title = QStringLiteral("Think Bayes");
        QVERIFY2(!categoryFor(QString(), other).has_value(), "a title with no shelf word stays uncategorised");
    }

    void matchingIsOnWholeWords() {
        // "art" must not fire on "Martin" or "startup"; "war" not on "software".
        FileMetadata meta;
        meta.title = QStringLiteral("Martin's Startup Software");
        QCOMPARE(categoryFor(QString(), meta).value(), QStringLiteral("Programming"));
    }

    void aPersonsNameIsNeverACategory() {
        // Gutenberg PDF keyword lists name the proofreaders.
        const FileMetadata meta = withSubjects({
            QStringLiteral("Paula Appling"),
            QStringLiteral("Andrew D. Hwang"),
            QStringLiteral("Project Gutenberg Online Distributed Proofreading Team"),
        });
        QVERIFY(!categoryFor(QString(), meta).has_value());
        QVERIFY(looksLikeAName(QStringLiteral("Andrew D. Hwang")));
        QVERIFY(!looksLikeAName(QStringLiteral("Mathematical recreations")));
        QVERIFY(!looksLikeAName(QStringLiteral("Numerals")));
    }

    void innerCapitalsSurviveTitleCasing() {
        QCOMPARE(categoryFor(QString(), withSubjects({QStringLiteral("Morphology (Animals)")})).value(),
            QStringLiteral("Morphology (Animals)"));
    }

    void headsAreCutAtEitherSeparator() {
        QCOMPARE(head(QStringLiteral("FICTION / Science Fiction / General")), QStringLiteral("FICTION"));
        QCOMPARE(head(QStringLiteral("Astronomy -- Early works to 1800")), QStringLiteral("Astronomy"));
        QCOMPARE(head(QStringLiteral("Fiction--History")), QStringLiteral("Fiction"));
        QCOMPARE(head(QStringLiteral("  Plain  ")), QStringLiteral("Plain"));
    }
};
