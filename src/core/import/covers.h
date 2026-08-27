// Cover storage, ported from omabook-core/src/import/covers.rs. Covers
// live outside the database, named by content hash, so that re-importing
// the same book reuses the file rather than duplicating it.
#pragma once

#include "core/db/database.h"
#include "core/import/metadata.h"
#include "core/result.h"

#include <QString>

// Where covers live: {Db::dataDir()}/covers.
QString coversDir();

// Write a cover and return its absolute path, thumbnailing it to fit
// inside 320x480 on the way in -- the Rust build stored covers at their
// original, sometimes print-resolution, size; nothing in the library grid
// or the reader needs more than that. A no-op, returning the existing
// path, when a cover with this hash and extension already exists: the
// file hash is already the book's identity, so nothing about its cover can
// have changed underneath an unchanged hash.
Result<QString> store(const QString &fileHash, const Cover &cover);

// Extensions come from inside a zip and are therefore untrusted input:
// strip anything that is not an ASCII letter or digit, keep at most five
// of them, lowercase, and fall back to "jpg" rather than let a
// path-traversal-shaped extension ("../../etc/passwd") reach the
// filesystem.
QString sanitiseExtension(const QString &extension);

// A `file://` URL, which is what QML's Image source needs.
QString toFileUrl(const QString &path);
