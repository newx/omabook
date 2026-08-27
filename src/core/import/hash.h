// Content hashing, ported from omabook-core/src/import/hash.rs. This hash
// is the identity of a book (SPEC §5.2): moving or renaming a file and
// rescanning must be a no-op, which is why identity comes from what the
// file contains rather than where it sits.
#pragma once

#include "core/result.h"

#include <QString>

// Hash at most this much of a file. Book files run to tens of megabytes
// and hashing every byte of a whole library is the slowest part of a
// scan; the first chunk plus the exact length is ample to tell distinct
// books apart.
constexpr qint64 HASH_PREFIX_BYTES = 1024 * 1024;

// A stable content identifier: sha256(length as 8 little-endian bytes,
// then the first HASH_PREFIX_BYTES of content), hex encoded. The length is
// folded in precisely so a truncated download never collides with the
// complete file it was cut from.
Result<QString> hashFile(const QString &path);
