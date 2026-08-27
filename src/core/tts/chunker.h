// Splitting a page into speakable chunks, ported from
// omabook-core/src/tts/chunker.rs.
#pragma once

#include <QStringList>
#include <QString>

namespace Tts {

// The constants are medialib's, and they were measured rather than guessed:
// 2000 characters take ~48s to synthesize before a single word is heard, and
// 4000 take ~98s and return 11 MB. Synthesis runs at roughly 2.4x realtime,
// so a deliberately small first chunk starts the audio in a few seconds and
// every later chunk is ready well before it is needed (SPEC §5.4). Do not
// change these without re-measuring.

// The first chunk is small so playback starts quickly.
constexpr int FIRST_CHUNK_CHARS = 220;
// Later chunks are larger; by then synthesis is running ahead of playback.
constexpr int CHUNK_CHARS = 600;

// Split text at sentence boundaries, respecting the two limits above.
QStringList chunk(const QString &text);

} // namespace Tts
