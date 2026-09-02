// Metadata from MOBI and AZW3 files.
//
// Both are PalmDOC databases whose first record carries a MOBI header and,
// behind it, an EXTH block: a flat list of typed records that is where the
// author, subjects, ISBN and description live. Reading that list is all the
// introspection these formats need; foliate-js does the rendering.
//
// Done by hand rather than through calibre's `ebook-meta` because calibre is
// a 400 MB optional dependency and a title, an author and a few subjects are
// not worth it. The layout is stable, documented, and a few dozen lines.
#pragma once

#include "core/import/metadata.h"
#include "core/result.h"

#include <QString>

namespace Mobi {

Result<FileMetadata> readMetadata(const QString &path);

} // namespace Mobi
