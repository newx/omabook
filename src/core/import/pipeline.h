// The import pipeline: Scan -> Hash -> Ingest -> ExtractText -> Classify ->
// Tag (SPEC §5.2), ported from omabook-core/src/import/pipeline.rs.
//
// Each book is handled independently, so one unreadable file cannot abort
// an import, and every step is idempotent: re-running over the same
// directory adds nothing and changes nothing. This runs on the import
// worker thread (CLAUDE.md, "Threading"), so it takes the QSqlDatabase &
// it is given rather than reaching for a connection of its own.
#pragma once

#include "core/import/metadata.h"
#include "core/models/book.h"
#include "core/result.h"

#include <QPair>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include <functional>
#include <optional>

// Tags attached from one book's dc:subject values, capped so one
// over-enthusiastic publisher's subject list cannot flood the sidebar
// (SPEC §5.2, "Tag").
constexpr int MAX_SUBJECT_TAGS = 6;

// What one importDirectory() run did, reported to the UI rather than
// logged and forgotten.
struct ImportReport {
    int scanned = 0;
    int imported = 0;
    int alreadyPresent = 0;
    QVector<QPair<QString, QString>> failed; // path, reason

    // "N imported, M already present, K failed" -- naming only what
    // actually happened; any part whose count is zero is omitted rather
    // than printed as "0 something".
    QString summary() const;
};

// Import every book under `root` into the library. `progress` is called
// with (index, total, filename) *before* each file is imported, so the UI
// can show what is happening on a large library. A missing root is an
// empty, successful run -- see scan(). One file's failure is recorded in
// ImportReport::failed with its reason; the loop continues rather than
// aborting the run.
Result<ImportReport> importDirectory(QSqlDatabase &db, const QString &root,
    std::function<void(int index, int total, const QString &name)> progress);

// The metadata as the classifier should see it: the display title -- the
// file's declared title, or the tidied filename when the declared one is
// junk (FileMetadata::isJunkTitle) -- rather than whatever the file
// declared, and no cover, which the classifier has no use for. Exposed
// because the app re-runs classification on its own for a single book.
FileMetadata classifiable(const FileMetadata &meta, const QString &path);

// Attach up to MAX_SUBJECT_TAGS taggable subjects from `meta` to `bookId`,
// creating each tag on first use. Exposed because the app re-runs tagging
// on its own for a single book, e.g. after a metadata refresh.
Result<void> attachSubjectTags(QSqlDatabase &db, qint64 bookId, const FileMetadata &meta);

// Reads a book file's metadata, and where the format allows it, an initial
// text-quality assessment, by dispatching on `format`. A read failure logs
// a warning and falls back to a default FileMetadata rather than aborting
// the import (SPEC §5.2, "ExtractText"). Exposed because the app re-runs
// extraction on its own for a single book.
QPair<FileMetadata, std::optional<TextQuality>> readFileMetadata(const QString &path, BookFormat format);
