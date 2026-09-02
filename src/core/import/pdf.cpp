#include "core/import/pdf.h"

#include "core/import/classify.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>

#include <algorithm>
#include <unistd.h>

namespace Pdf {

namespace {

// Below this many characters on a sampled page, the extraction is
// worthless and the file is almost certainly a scan (SPEC §7.2).
constexpr int SCANNED_CHAR_THRESHOLD = 120;
// Above this share of lines repeating across pages, running headers and
// footers dominate and TTS would read them aloud on every page.
constexpr double NOISY_REPEAT_RATIO = 0.35;

// Every external process gets a timeout so a wedged or missing poppler
// cannot hang the import pipeline (CLAUDE.md, "External processes").
constexpr int PROCESS_TIMEOUT_MS = 20000;

// Runs a poppler tool and returns its stdout on success. A missing binary
// or a non-zero exit is a degraded feature, not a crash (SPEC §6.2): the
// caller sees nullopt either way. errorOccurred is connected explicitly
// (not just relying on waitForStarted's return) because that is the
// signal a missing binary actually emits -- CLAUDE.md, "External
// processes". stdout is drained via readyReadStandardOutput as it arrives
// rather than read in one shot at the end, so a large PDF's output cannot
// fill the pipe and stall the child.
std::optional<QString> run(const QString &program, const QStringList &args) {
    QProcess process;
    QByteArray output;
    bool startFailed = false;

    QObject::connect(&process, &QProcess::errorOccurred,
                      [&](QProcess::ProcessError) { startFailed = true; });
    QObject::connect(&process, &QProcess::readyReadStandardOutput,
                      [&]() { output += process.readAllStandardOutput(); });

    process.start(program, args);
    if (!process.waitForStarted(PROCESS_TIMEOUT_MS) || startFailed)
        return std::nullopt;

    if (!process.waitForFinished(PROCESS_TIMEOUT_MS)) {
        process.kill();
        process.waitForFinished(1000);
        return std::nullopt;
    }
    output += process.readAllStandardOutput();

    if (startFailed || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return std::nullopt;

    return QString::fromUtf8(output);
}

// A keyword list split on the separators publishers actually use. Both
// Subject and Keywords are free text in practice, most often a comma
// list: "math, mathematics, formulas". Split into subjects so they
// classify and tag the same way dc:subject does; a one-off phrase
// ("Version 2.6.3") survives as a single subject and is filtered
// downstream like any other. Proofreaders and typesetters credit
// themselves here, so anything that looks like a person's name is
// dropped.
QStringList splitKeywords(const QString &value) {
    QStringList out;
    const QStringList parts = value.split(QRegularExpression(QStringLiteral("[,;]")));
    for (const QString &part : parts) {
        const QString trimmed = part.trimmed();
        if (trimmed.isEmpty() || looksLikeAName(trimmed))
            continue;
        out.append(trimmed);
    }
    return out;
}

// Renders page 1 to a PNG for use as the cover. Any failure -- missing
// poppler, a broken PDF, a write error -- is no cover, not an error.
std::optional<Cover> renderFirstPage(const QString &path) {
    const QString dirPath = QDir::temp().filePath(QStringLiteral("omabook-pdf-cover"));
    if (!QDir().mkpath(dirPath))
        return std::nullopt;
    const QString stem = QDir(dirPath).filePath(QStringLiteral("cover-%1").arg(getpid()));

    QProcess process;
    bool startFailed = false;
    QObject::connect(&process, &QProcess::errorOccurred,
                      [&](QProcess::ProcessError) { startFailed = true; });

    process.start(QStringLiteral("pdftoppm"),
                  { QStringLiteral("-png"), QStringLiteral("-f"), QStringLiteral("1"), QStringLiteral("-l"),
                    QStringLiteral("1"), QStringLiteral("-r"), QStringLiteral("80"), QStringLiteral("-singlefile"),
                    path, stem });
    if (!process.waitForStarted(PROCESS_TIMEOUT_MS) || startFailed)
        return std::nullopt;
    if (!process.waitForFinished(PROCESS_TIMEOUT_MS)) {
        process.kill();
        process.waitForFinished(1000);
        return std::nullopt;
    }
    if (startFailed || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return std::nullopt;

    const QString rendered = stem + QStringLiteral(".png");
    QFile file(rendered);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QByteArray bytes = file.readAll();
    file.close();
    QFile::remove(rendered);

    if (bytes.isEmpty())
        return std::nullopt;
    return Cover{ bytes, QStringLiteral("png") };
}

} // namespace

Result<FileMetadata> readMetadata(const QString &path) {
    FileMetadata meta;

    const std::optional<QString> info = run(QStringLiteral("pdfinfo"), { path });
    if (info) {
        const QStringList lines = info->split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            const int colon = line.indexOf(QLatin1Char(':'));
            if (colon < 0)
                continue;
            const QString key = line.left(colon).trimmed();
            const QString value = line.mid(colon + 1).trimmed();
            if (value.isEmpty())
                continue;

            if (key == QLatin1String("Title")) {
                meta.title = value;
            } else if (key == QLatin1String("Author")) {
                meta.authors = QStringList{ value };
            } else if (key == QLatin1String("Subject") || key == QLatin1String("Keywords")) {
                meta.subjects.append(splitKeywords(value));
            } else if (key == QLatin1String("Producer")) {
                if (meta.publisher.isEmpty())
                    meta.publisher = value;
            }
        }
    }

    meta.cover = renderFirstPage(path);
    return Result<FileMetadata>::ok(meta);
}

std::optional<QString> extractText(const QString &path, int first, int last) {
    return run(QStringLiteral("pdftotext"),
               { QStringLiteral("-f"), QString::number(first), QStringLiteral("-l"), QString::number(last),
                 QStringLiteral("-layout"), path, QStringLiteral("-") });
}

std::optional<int> pageCount(const QString &path) {
    const std::optional<QString> info = run(QStringLiteral("pdfinfo"), { path });
    if (!info)
        return std::nullopt;

    const QStringList lines = info->split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (!line.startsWith(QLatin1String("Pages:")))
            continue;
        bool ok = false;
        const int pages = line.mid(6).trimmed().toInt(&ok);
        if (ok)
            return pages;
    }
    return std::nullopt;
}

TextQuality assessTextQuality(const QString &path) {
    const std::optional<int> pages = pageCount(path);
    if (!pages || *pages <= 0)
        return TextQuality::None_;

    // Sample from the middle: front matter is unrepresentative.
    const int samples[3] = { std::max(*pages / 4, 1), std::max(*pages / 2, 1), std::max((*pages * 3) / 4, 1) };

    QStringList texts;
    for (int page : samples) {
        const std::optional<QString> text = extractText(path, page, page);
        if (text)
            texts.append(*text);
    }
    if (texts.isEmpty())
        return TextQuality::None_;

    // QString::size() counts UTF-16 code units rather than Unicode
    // codepoints; for the mostly-Latin text pdftotext produces the difference
    // is immaterial to a threshold this coarse.
    qint64 totalChars = 0;
    for (const QString &text : texts)
        totalChars += text.trimmed().size();
    const qint64 average = totalChars / texts.size();
    if (average < SCANNED_CHAR_THRESHOLD)
        return TextQuality::None_;

    // Text that is not language is worse than noisy text: TTS would read
    // it aloud as nonsense and RAG would index it as nonsense.
    for (const QString &text : texts) {
        if (looksLikeGibberish(text))
            return TextQuality::Poor;
    }

    if (repeatedLineRatio(texts) > NOISY_REPEAT_RATIO)
        return TextQuality::Poor;

    return TextQuality::Good;
}

} // namespace Pdf
