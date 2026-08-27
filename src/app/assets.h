// Locating the reader assets (foliate-js and reader.html).
//
// They ship as package data rather than embedded in the binary, which is the
// ordinary Arch layout -- omawrite installs its icon under /usr/share the same
// way (SPEC 3.4). Loading the reader from file:// is also what lets it fetch
// the book file directly, which a qrc: page cannot do.
#pragma once

#include <QString>

namespace Assets {

// Where the reader lives, in order of preference:
//   1. $OMABOOK_READER_DIR, for development and testing
//   2. /usr/share/omabook/reader, where the package installs it
//   3. {dataDir}/reader, for a per-user install that needs no root
//   4. assets/reader above the executable, for a build in the source tree
// Empty when none of them has a reader.html.
QString readerDir();

// A file:// URL for the reader, with the book and start position attached.
// Empty when readerDir() found nothing.
QString readerUrl(const QString &bookPath, const QString &cfi);
} // namespace Assets
