// Directory scanning.
#pragma once

#include "core/models/book.h"

#include <QString>
#include <QVector>

// A book file found on disk, before anything has been read from it.
struct Candidate {
    QString path;
    BookFormat format;
    qint64 size = 0;
};

// Find every readable book file under `root`, recursively, not following
// symlinks. A missing root yields an empty result, not an error -- the
// caller already knows the path it asked for. An unreadable directory is
// skipped with a warning rather than aborting the whole scan: one bad
// permission should not cost the user their whole import. Results are
// sorted by path, so a run is reproducible.
QVector<Candidate> scan(const QString &root);
