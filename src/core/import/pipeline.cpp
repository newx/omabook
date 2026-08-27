#include "core/import/pipeline.h"

#include "core/import/classify.h"
#include "core/import/covers.h"
#include "core/import/epub.h"
#include "core/import/hash.h"
#include "core/import/mobi.h"
#include "core/import/pdf.h"
#include "core/import/scanner.h"
#include "core/repo/bookrepository.h"
#include "core/repo/taxonomy.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace {

// PDF keyword fields carry things that are not tags: proofreaders' names,
// version strings, publisher boilerplate. A tag is a short phrase.
bool isTaggable(const QString &subject) {
    const QStringList words =
            subject.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    const QString lower = subject.toLower();
    return words.size() >= 1 && words.size() <= 5 && subject.size() <= 60
            && !lower.contains(QLatin1String("version")) && !lower.contains(QLatin1String("gutenberg"))
            && !lower.contains(QLatin1String("proofread"));
}

// The first path component below `root`, or an empty string when the file
// sits loose in the root or is not actually under it. Both sides are
// resolved to absolute paths first so that a relative `root` (as tests
// pass) still lines up with the absolute paths scan() returns.
QString categoryFromPath(const QString &root, const QString &path) {
    const QString normalizedRoot = QDir(root).absolutePath();
    const QString normalizedPath = QFileInfo(path).absoluteFilePath();
    if (!normalizedPath.startsWith(normalizedRoot))
        return QString();

    QString relative = normalizedPath.mid(normalizedRoot.size());
    while (relative.startsWith(QLatin1Char('/')))
        relative.remove(0, 1);

    // A file directly in the root has no directory to name it.
    const QStringList parts = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() < 2)
        return QString();
    return parts.first();
}

enum class ImportOutcome { Imported, AlreadyPresent };

// Everything from "hash the file" through "tag it", for exactly one
// candidate. Any failure along the way propagates to the caller, which
// records it against this file and continues with the next one -- one bad
// book must never abort the run.
Result<ImportOutcome> importOne(QSqlDatabase &db, BookRepository &books, const QString &root,
        const Candidate &candidate) {
    const Result<QString> hashResult = hashFile(candidate.path);
    if (hashResult.isErr())
        return hashResult.error();
    const QString fileHash = hashResult.value();

    // Import is idempotent by content hash, deliberately: a fixed
    // extractor does not retroactively improve rows already imported --
    // they must be deliberately refreshed later (SPEC §5.2).
    const Result<std::optional<Book>> existing = books.findByHash(fileHash);
    if (existing.isErr())
        return existing.error();
    if (existing.value().has_value())
        return Result<ImportOutcome>::ok(ImportOutcome::AlreadyPresent);

    const QPair<FileMetadata, std::optional<TextQuality>> read =
            readFileMetadata(candidate.path, candidate.format);
    const FileMetadata &meta = read.first;
    const std::optional<TextQuality> textQuality = read.second;

    // A cover failure is a warning and no cover, not a failed import.
    QString coverPath;
    if (meta.cover.has_value()) {
        const Result<QString> stored = store(fileHash, *meta.cover);
        if (stored.isOk()) {
            coverPath = stored.value();
        } else {
            qWarning() << "could not store cover for" << candidate.path << ":"
                       << stored.error().message;
        }
    }

    NewBook newBook;
    newBook.title = meta.titleOrFilename(candidate.path);
    newBook.authors = meta.authors;
    newBook.sourcePath = candidate.path;
    newBook.format = candidate.format;
    newBook.fileHash = fileHash;
    newBook.fileSize = candidate.size;
    newBook.description = meta.description;
    newBook.publisher = meta.publisher;
    newBook.language = meta.language;
    newBook.publishedDate = meta.publishedDate;
    newBook.isbn13 = meta.isbn13;
    newBook.coverPath = coverPath;
    newBook.textQuality = textQuality;
    if (candidate.format == BookFormat::Pdf) {
        const std::optional<int> pages = Pdf::pageCount(candidate.path);
        if (pages.has_value())
            newBook.pageCount = static_cast<qint64>(*pages);
    }

    const Result<qint64> inserted = books.insert(newBook);
    if (inserted.isErr())
        return inserted.error();
    const qint64 bookId = inserted.value();

    // The folder the book sits in, when there is one, then the file's own
    // subjects -- see classify.h for the order and why.
    const QString folder = categoryFromPath(root, candidate.path);
    const std::optional<QString> categoryName = categoryFor(folder, classifiable(meta, candidate.path));
    if (categoryName.has_value()) {
        CategoryRepository categories(db);
        const Result<qint64> categoryId = categories.ensure(*categoryName);
        if (categoryId.isErr())
            return categoryId.error();
        const Result<void> assigned = categories.assign(bookId, categoryId.value());
        if (assigned.isErr())
            return assigned.error();
    }

    const Result<void> tagged = attachSubjectTags(db, bookId, meta);
    if (tagged.isErr())
        return tagged.error();

    return Result<ImportOutcome>::ok(ImportOutcome::Imported);
}

} // namespace

QString ImportReport::summary() const {
    QStringList parts;
    if (imported > 0)
        parts.append(QStringLiteral("%1 imported").arg(imported));
    if (alreadyPresent > 0)
        parts.append(QStringLiteral("%1 already present").arg(alreadyPresent));
    if (!failed.isEmpty())
        parts.append(QStringLiteral("%1 failed").arg(failed.size()));
    return parts.join(QStringLiteral(", "));
}

FileMetadata classifiable(const FileMetadata &meta, const QString &path) {
    // The classifier should see the display title, not whatever junk the
    // file declared, and has no use for a cover.
    FileMetadata result = meta;
    result.title = meta.titleOrFilename(path);
    result.cover = std::nullopt;
    return result;
}

Result<void> attachSubjectTags(QSqlDatabase &db, qint64 bookId, const FileMetadata &meta) {
    TagRepository tags(db);
    int attached = 0;
    for (const QString &subject : meta.subjects) {
        if (attached >= MAX_SUBJECT_TAGS)
            break;
        if (!isTaggable(subject))
            continue;

        const Result<qint64> tagId = tags.ensure(subject);
        if (tagId.isErr())
            return tagId.error();
        const Result<void> attachResult = tags.attach(bookId, tagId.value(), QStringLiteral("file"));
        if (attachResult.isErr())
            return attachResult.error();
        ++attached;
    }
    return Result<void>::ok();
}

QPair<FileMetadata, std::optional<TextQuality>> readFileMetadata(const QString &path, BookFormat format) {
    switch (format) {
    case BookFormat::Epub: {
        FileMetadata meta;
        const Result<FileMetadata> result = Epub::readMetadata(path);
        if (result.isOk()) {
            meta = result.value();
        } else {
            qWarning() << "could not read EPUB metadata for" << path << ":" << result.error().message
                       << "; falling back to filename";
        }
        // EPUB text is structured markup; extraction is reliable.
        return qMakePair(meta, std::optional<TextQuality>(TextQuality::Good));
    }
    case BookFormat::Pdf: {
        const Result<FileMetadata> result = Pdf::readMetadata(path);
        const FileMetadata meta = result.isOk() ? result.value() : FileMetadata();
        return qMakePair(meta, std::optional<TextQuality>(Pdf::assessTextQuality(path)));
    }
    case BookFormat::Mobi:
    case BookFormat::Azw3: {
        FileMetadata meta;
        const Result<FileMetadata> result = Mobi::readMetadata(path);
        if (result.isOk()) {
            meta = result.value();
        } else {
            qWarning() << "could not read MOBI metadata for" << path << ":" << result.error().message
                       << "; falling back to filename";
        }
        return qMakePair(meta, std::optional<TextQuality>());
    }
    default:
        // Comics are pictures; there is nothing to read out of them.
        return qMakePair(FileMetadata(), std::optional<TextQuality>());
    }
}

Result<ImportReport> importDirectory(QSqlDatabase &db, const QString &root,
        std::function<void(int index, int total, const QString &name)> progress) {
    const QVector<Candidate> candidates = scan(root);
    const int total = candidates.size();

    ImportReport report;
    report.scanned = total;

    BookRepository books(db);

    for (int index = 0; index < total; ++index) {
        const Candidate &candidate = candidates.at(index);
        const QString name = QFileInfo(candidate.path).fileName();
        if (progress)
            progress(index, total, name);

        const Result<ImportOutcome> outcome = importOne(db, books, root, candidate);
        if (outcome.isErr()) {
            qWarning() << "import failed for" << candidate.path << ":" << outcome.error().message;
            report.failed.append(qMakePair(candidate.path, outcome.error().message));
            continue;
        }

        if (outcome.value() == ImportOutcome::Imported)
            ++report.imported;
        else
            ++report.alreadyPresent;
    }

    return Result<ImportReport>::ok(report);
}
