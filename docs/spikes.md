# C++ port spikes

Recorded 2026-08-27 on Omarchy, Qt 6.11.2, GCC on Arch. Each spike exists to
settle a decision empirically rather than by argument, before any of the port
is written on top of it.

## qmake builds the whole module set — PASSED

`cmake` is not installed on this machine and `omawrite` uses qmake, so the
question was whether qmake can carry a project this much larger. A `.pro` with

```
QT += core gui qml quick quickcontrols2 quickdialogs2 webenginequick \
      webchannel multimedia sql network texttospeech dbus concurrent core-private
CONFIG += c++17 release
```

configures, compiles and links on Qt 6.11.2, and the resulting binary resolves
the QML imports `QtWebEngine`, `QtTextToSpeech` and `QtMultimedia` at runtime.

**Verdict: qmake, matching omawrite.** No second build system to learn, and the
`.pro` stays a flat file list.

## QSQLITE gives us everything the Rust build used rusqlite for — PASSED

The worry was that Qt's bundled SQLite driver would be a cut-down build and
force us to link `sqlite3` ourselves. It is not. Against the QSQLITE driver:

```
sql drivers   : QIBASE,QSQLITE,QMARIADB,QMYSQL,QODBC,QPSQL
PRAGMA journal_mode=WAL   -> wal
CREATE VIRTUAL TABLE f USING fts5(a, tokenize='unicode61 remove_diacritics 2')  -> ok
VACUUM INTO '<path>'      -> ok
sqlite_version()          -> 3.53.4
```

FTS5 with diacritic folding is what the search box needs (§5.1), `VACUUM INTO`
is backup layer 1 (§5.11), and WAL is the concurrency model. All present.

**Verdict: `QSqlDatabase` / `QSqlQuery`, no direct sqlite3 dependency.**

## QZipReader reads real EPUBs — PASSED, with a caveat

Qt has no public ZIP API. `QtCore/private/qzipreader_p.h` (`QT += core-private`)
compiles, links, and lists and extracts entries from a real EPUB:

```
mimetype 20
META-INF/container.xml 253
EPUB/wasteland.opf 2109
...
container.xml bytes: 253
```

**Caveat, and qmake says so out loud:** private headers tie the binary to the
exact Qt patch version it was compiled against. On a rolling distribution that
means a `qt6-base` update can break a binary that is not rebuilt. Acceptable
for a package rebuilt from source by `makepkg`; it would not be for a binary
shipped once.

## WebEngine does not run headless

Carried over from the Rust build's Phase 0 and still true: `QT_QPA_PLATFORM=offscreen`
never loads a `WebEngineView`, and the run is silent rather than failing. Any
reader verification has to happen on the real Wayland session, and the test
binary must not depend on the reader.
