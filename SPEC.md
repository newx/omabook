# omabook — Specification (C++ / Qt 6)

A personal book library and reader, in the spirit of Apple Books, built as a
native Omarchy app in the idiom of `omawrite` and `omacalc`.

**Status:** the specification for this branch, with the AI and speech features
removed.
**Branch:** `main`. The full application, with the assistant and reading
aloud, is `main-with-ai-features`.
**Date:** 2026-08-27
**Location:** `~/Projects/omabook`

Sections §5.4 to §5.8, §6.2 and §7.4 describe features this branch removes.
They are kept as stubs rather than deleted so the numbering — which code
comments cite as `SPEC §x` — still means the same thing on both branches.

Section numbers are load-bearing: code comments cite them as `SPEC §x`, so a
section is renumbered only when its `SPEC §x` citations are updated with it.

---

## 1. Product summary

A native Qt Quick desktop application. One binary, no server, no daemon, no
login. Launch it from the app launcher, read a book, close it, and nothing is
left running.

- Holds a library of books (EPUB, PDF, MOBI/AZW3) imported from a local directory.
- Organizes them by **categories** (a tree, one per book) and **tags** (many per
  book), assigned at import and editable by hand.
- Reads them with real pagination and position memory.
- Tracks reading progress and completion, and keeps an ordered reading queue.
- Holds highlights and notes, anchored to the passage they came from.

Single user, single machine, and **no network at all** — no model, no speech
service, no metadata lookup. A headless `omabook serve` mode for reading on a
tablet or phone is deliberately deferred (§6.3).

---

## 2. Stack decision

### 2.1 Why this stack

**Plain C++17 and Qt 6, built with qmake, in omawrite's shape.**

`omawrite`, `omacalc`, `omacut` and `omasnap` are C++ and QML built with qmake,
and "would Omarchy ship it" (§3.3) is a question about fit as much as about
quality. Building the way the family builds also means one set of failure modes
rather than three: no code generator sitting between the C++ and the QML, no
second build system, and one place to look when a QML file does not resolve.

### 2.2 EPUB forces a web engine, and that is unavoidable

There is no QML EPUB engine from any vendor. The reader surface is a QML
`WebEngineView` running **foliate-js** — the only maintained web library
handling EPUB, MOBI, AZW3, FB2, CBZ *and* PDF behind one API, with pagination,
CFI addressing, and annotations.

That means `qt6-webengine`: 282 MB installed. Nothing about the choice of
language changes it. EPUB is HTML and CSS, so something has to lay out HTML and
CSS.

`QtWebEngineQuick::initialize()` must run in `main()` before the application
object is constructed. That is one line at the top of `main()`, and it is the
one requirement a Quickshell plugin could never satisfy — an application
controls its own `main()`.

### 2.3 What is not written here

Four things are taken as given rather than reimplemented, and changing any of
them is a decision, not a refactor:

- **foliate-js**, vendored verbatim in `assets/reader/foliate-js` and never
  patched in place (CLAUDE.md, "Setup").
- **`assets/reader/reader.html`** and its two polyfill files. This is the reader
  protocol (§5.3), and it is JavaScript on both sides of the channel.
- **The prompt set** (§5.6–5.8), verbatim, including its punctuation.
- **The measured constants** — the text-quality thresholds and the completion
  threshold. These were arrived at against a real library and are not to be
  re-derived.

### 2.4 Full stack

| Layer | Choice | Why |
|---|---|---|
| Language | **C++17** | matches omawrite; `CONFIG += c++17` |
| UI | QML / Qt Quick Controls 2 | |
| Build | **qmake** (`omabook.pro`) | omawrite's toolchain; verified on the full module set (`docs/spikes.md`) |
| DB | **`QSqlDatabase` / `QSQLITE`** | WAL, FTS5 with `remove_diacritics 2`, and `VACUUM INTO` all verified present |
| Reader | `WebEngineView` + foliate-js | §2.2 |
| Host ⇄ reader | `QWebChannel` | §5.3 |
| EPUB zip | **`QZipReader`** (`QtCore/private/qzipreader_p.h`, `QT += core-private`) | verified against real EPUBs; stored+deflate only, which is all EPUB permits |
| EPUB XML | **`QXmlStreamReader`** | strict — needs namespace matching and an entity resolver (§5.2) |
| PDF text | `pdftotext` (process) | now only for the text-quality heuristic (§7.2) |
| PDF covers | `pdftoppm` (process) | |
| MOBI/AZW3 | `ebook-convert` (process) | |
| Covers | **`QImageReader` + `setScaledSize`** | thumbnailed on the way in (§5.2) |
| Jobs | **worker `QObject` on a `QThread`** | see §6.4 |
| Errors | **`core/result.h`** (§8) | no exceptions anywhere |
| Packaging | **PKGBUILD, `bin/build`** | as omawrite does it |

### 2.5 Repository layout

**One repo, one `.pro`, one `make`, one installed binary.**

```
omabook/
├── omabook.pro              # the only build target
├── CLAUDE.md  SPEC.md  TODO.md  README.md  LICENSE
├── bin/                     # build, test, install
├── src/
│   ├── core/                # no QML, ever. Enforced by tests/tests.pro
│   │   ├── core.pri
│   │   ├── result.h  error taxonomy
│   │   ├── db/              # Database, migrations, migrations/*.sql
│   │   ├── models/          # Book, Note, enums
│   │   ├── repo/            # BookRepository, NoteRepository, Taxonomy, Settings
│   │   ├── import/          # scanner, hash, metadata, epub, mobi, pdf,
│   │   │                    #   covers, classify, pipeline
│   │   └── omarchy.{h,cpp}  # the active Omarchy theme, and watching it
│   └── app/
│       ├── app.pri  app.qrc
│       ├── main.cpp  assets.cpp
│       ├── bridge/          # LibraryModel, ReaderBridge, SidebarModel,
│       │                    #   NotesModel, ThemeModel
│       └── qml/             # 23 files, aliased flat into qrc:/
├── tests/tests.pro  tests/tst_omabook.cpp
├── assets/
│   ├── omabook.svg  omabook-mark.png
│   └── reader/              # reader.html, polyfills, vendored foliate-js
├── pkgbuild/
└── docs/
```

**The core/app boundary.** The boundary is the module list in
`tests/tests.pro`, which omits
`qml`, `quick`, `webenginequick` and `multimedia`, so those include paths do not
exist and `#include <QQmlEngine>` in core is a compile error. `bin/test` runs
that build every time, and additionally greps for the fully-qualified include
form that would slip past it. See CLAUDE.md for the exact rule.

**Only `omabook.pro` produces a binary.** QML is embedded through the resource
system. The reader assets are **not** embedded — see §5.3.

### 2.6 Hardware

Developed on an eight-core desktop with integrated graphics and no discrete
GPU. That mattered when there was inference to run; it does not now. Nothing on
this branch is compute-bound, and the heaviest thing an import does is render a
PDF cover.

---

## 3. The Omarchy way

### 3.1 Omarchy ships a family of native apps

`omacalc`, `omacut`, `omawrite`, `omasnap`, `omatrack` — Qt Quick is the idiom
for GUI apps in `[omarchy]`, and the repository accepts community apps.
`omabook` fits the naming convention exactly, and differs from `omawrite` in
size and dependencies rather than in construction.

### 3.2 What omawrite establishes

`omawrite` is C++ and QML, built with qmake, in its own repo under `omacom-io`,
with `pkgbuild/` in the repo. Integration is by **convention, not API**: follow
the system dark/light mode, honour `omarchy display text size` live, carry
`StartupWMClass` and a `MimeType` list in the `.desktop`.

Its house style is the subject of CLAUDE.md, and is now binding here.

### 3.3 Where omabook fits, and where it cannot

Achievable: a native `oma*` app in the `[omarchy]` repository. Not
achievable: default-install status, because QtWebEngine is 282 MB. The target is
"available and worth installing".

### 3.4 Packaging and distribution

Own git repo, `pkgbuild/PKGBUILD` in it, `bin/install` wrapping `makepkg -fsi`.

```
depends=(hicolor-icon-theme poppler qt6-base qt6-declarative qt6-imageformats
         qt6-svg qt6-webchannel qt6-webengine xdg-desktop-portal)
makedepends=(qt6-base)
optdepends=(calibre)
```

Two of these are silent-failure dependencies and are easy to leave out:
`qt6-multimedia-ffmpeg` (without it `QMediaPlayer` plays nothing) and
`qt6-imageformats` (without it WebP covers decode to nothing).

### 3.5 Integration conventions to follow

Follow system dark/light; honour text scale live; `StartupWMClass=omabook`;
`MimeType=application/epub+zip;application/x-mobipocket-ebook;application/pdf;`
with `Exec=omabook %f`; publish MPRIS while reading aloud; match the active
Omarchy theme's accent.

---

## 4. Data model

SQLite, WAL mode, at `~/.local/share/omabook/omabook.db`. Settings in
`~/.config/omabook/`, cache in `~/.cache/omabook/`, covers beside the database.

**Migrations are append-only.** All five live as `.sql` files under
`src/core/db/migrations/`, compiled into the binary through the resource system,
and applied in order against `PRAGMA user_version`. A database already on disk
must keep opening as the schema moves, and that is a test.

```
001_initial.sql          categories, tags, books, book_tags, reading_progress,
                         queue_items, book_chunks, summaries, notes, settings
002_favorites.sql        books.is_favorite + partial index
003_search_and_notes.sql notes rebuilt (cfi, quote, body, page_fraction,
                         page_label, color, updated_at); books_fts FTS5 with
                         tokenize='unicode61 remove_diacritics 2' and its three
                         sync triggers; backfill
004_embeddings.sql       chunk_embeddings, book_embeddings (BLOB vectors),
                         books.chunked_at
005_chunk_search.sql     chunks_fts FTS5 over book_chunks + sync triggers
006_drop_ai.sql          THIS BRANCH. Drops chunks_fts and its triggers,
                         chunk_embeddings, book_embeddings, book_chunks,
                         summaries, and books.chunked_at / embedded_at /
                         auto_tagged_at
```

**006 is one-way, and that is the point.** A library opened here loses its
embeddings; re-indexing on `main-with-ai-features` costs roughly two minutes a
book. Everything a reader actually writes — position, highlights, notes, the
queue, favourites — is untouched, so a book read on either branch reads fine on
the other. `metadata_fetched_at` stays: online metadata lookup is not an AI
feature and is still on the roadmap.

**The runner vacuums after applying anything.** `DROP TABLE` frees pages inside
the file but never shrinks it, so without that step an indexed library would sit
at its old size for ever. Measured on the real one: 45 MB before, **212 KB**
after, with all 51 books and 6 notes intact.

Points that are easy to lose and must not be:

- `books.authors` is a **JSON array as text** — `["Herman Melville"]`. The
  substring search path relies on that being plain text.
- `idx_queue_active_book` is **partial** (`WHERE removed_at IS NULL`), which is
  what lets a book be removed from the queue and added again.
- `idx_notes_book_cfi` is **partial and unique**, which is what makes
  highlighting a passage twice an update rather than a duplicate.
- Both FTS5 tables use the **delete-then-reinsert** update pattern, which is
  required for contentless and external-content tables.
- `notes.color` is written by nothing and read by nothing. Carry it anyway;
  migrations are append-only and dropping it would be a new migration for no
  gain.

### 4.1 On vector-search scale

**Removed with §5.7 and §5.8.** For the record, the design was brute-force
cosine over `f32` blobs rather than an ANN index, on the grounds that a personal
library is a few hundred thousand chunks at most and an index would be
optimising a millisecond. It stands on `main-with-ai-features`.

---

## 5. Features

### 5.1 Library

QML grid of covers. Filter by category, tag, status and format; sort by added,
title, author or progress.

**Search** is SQLite FTS5 over title, authors, description, series and
publisher, kept in step by triggers. Each typed word becomes a quoted prefix
term (`"moby"*`), joined by FTS5's implicit AND, so all words must match and
"mob" finds Moby-Dick before the word is finished. Diacritic folding comes
entirely from the table's `remove_diacritics 2` tokenizer, not from the query
builder, so "calculo" finds "Cálculo".

**A query FTS5 cannot parse falls back to a substring match rather than
erroring.** A lone quote, a bare `*`, an unbalanced operator — the failure is
caught and `LIKE '%query%'` answers instead. The strategy used is reported back
to the UI. A search box must never punish someone for typing punctuation, and
this is tested with exactly that input.

The Queue filter overrides the requested sort with the queue's own `position`,
because reordering a queue under any other sort is meaningless.

### 5.2 Import pipeline

`Scan → Hash → Ingest → Convert → ExtractText → Classify → Tag`, each step
idempotent, run on a worker thread with progress marshalled to QML.

- **Scan** — recursive walk, no symlink following, skip any path with a dotfile
  component, keep only known extensions, drop files under 1024 bytes (a
  truncated download), sort by path so a run is reproducible. A missing root is
  an empty result, not an error.
- **Hash** — SHA-256 over the file length as eight little-endian bytes followed
  by the first 1 MiB of content. A prefix hash for speed, with the length folded
  in so a truncated file does not collide with its complete original. This is
  the identity of a book: moving or renaming a file and rescanning is a no-op.
- **Ingest** — create the `books` row, extract the cover, thumbnail it, store it
  content-addressed at `{data}/covers/{hash}.{ext}`.
- **Convert** — MOBI/AZW3 → EPUB via `ebook-convert`. After this step the rest
  of the system knows only EPUB and PDF.
- **ExtractText** — EPUB through `QZipReader` + `QXmlStreamReader`; PDF through
  `pdftotext`. Sets `books.text_quality` (§7.2).
- **Classify** — offline, no model. The folder a book sits in wins outright;
  otherwise a keyword table over the subjects; otherwise the head of the first
  subject that looks like a subject and not like a person's name; otherwise the
  keyword table over title and description. Books with no signal stay
  uncategorised rather than guessed.
- **Tag** — `dc:subject` values, capped at six per book.

**Two details worth stating.**

*Covers are thumbnailed.* A cover arrives at whatever size the book carries — up
to a 12 MiB EPUB image, or whatever `pdftoppm -r 80` produces — and nothing in
the library grid or the reader needs that. `QImageReader` makes scaled decode
nearly free, so covers are scaled to fit 320×480 with `setScaledSize` computed
from `reader.size()` (header-only, no decode) and `setAutoTransform(true)` for
EXIF orientation, then written as JPEG at quality 85.

*XML parsing is strict.* `QXmlStreamReader` errors where laxer parsers shrug.
Three consequences, all mandatory: match on `namespaceUri()` plus local name and
never on `qualifiedName()`, because real OPF files arrive as `<package>`,
`<opf:package>` and worse; look up `href` and `media-type` in the **empty**
namespace while Dublin Core elements are in theirs; and install a
`QXmlStreamEntityResolver` returning a space for undeclared entities, because
sloppy converters emit `&nbsp;` in metadata and that is otherwise a hard parse
error on a file that would have imported fine.

**Zip-bomb defences carry over unchanged.** The declared entry size in the zip
header is never trusted: every read is capped and the *actual* length is
re-checked. 12 MiB per cover, 32 MiB per text entry, 256 MiB for a whole book's
text.

### 5.3 Reader

A QML page hosting a `WebEngineView` that loads vendored foliate-js. One reader
for every format. Reader chrome — toolbar, progress slider, chapter list — is
QML around the web view, not HTML inside it.

**The split is a hard constraint:** JavaScript owns rendering, pagination, CFI,
visible-range text extraction and `advanceToNextPage()`; C++ owns the database,
progress, and everything else. C++ never guesses
what is on screen — it asks. That is what keeps the backend format-agnostic.

**The protocol.** The page exposes `window.omabookNext`,
`omabookPrev`, `omabookGoTo`, `omabookPageText`, `omabookAdvance`,
`omabookSetAppearance`, `omabookSetAnnotations`, `omabookAddAnnotation`. It calls
back through `bridge.pageChanged(cfi, fraction, text, chapter, page)`,
`readerReady()`, `readerFailed(message)`, `saveHighlight(cfi, text, fraction)`
and `requestNote(cfi, text, fraction)`.

Load-bearing details:

- **`readerReady()` is the handshake.** The QWebChannel handshake completes
  *after* `LoadSucceeded`, so everything the host may call is defined first and
  the page then announces itself. A call issued earlier evaluates against
  `undefined`, returns `undefined`, and fails silently while the callback still
  fires — nothing looks wrong. This cost a day to find once. QML gates on
  `ReaderBridge.connected`, never on `LoadSucceededStatus`.
- **`omabookAdvance()` succeeds when the fraction changes, not when the promise
  resolves.** `view.next()` can resolve before the location has moved. It polls
  `lastLocation.fraction` every 100 ms against the value captured before the
  turn, for up to 4000 ms, and returns `null` on timeout.
- **PDF page numbers come from the section index, not the fraction.** A PDF
  section is weighted 1000 of 1500 location units regardless of its real page
  count, so the fraction drifts — page 69 once asked `pdftotext` for page 45.
  `page = section.current + 1` for fixed-layout books.
- **PDF page text never comes from the DOM.** A fixed-layout renderer keeps both
  halves of a spread in the DOM with one hidden, so JS cannot tell which is "this
  page". `visibleText()` returns `''` for PDFs and the host calls
  `pdftotext -f N -l N` instead. After a PDF page turn the host waits 250 ms for
  the page number to settle, then re-fetches.
- **Selection and key handlers attach to each chapter iframe as it loads.**
  foliate renders each section into its own iframe; a listener on the top
  document does not cross that boundary. The selection rectangle is mapped back
  through `frameElement.getBoundingClientRect()`.
- **`localContentCanAccessFileUrls: true` and
  `localContentCanAccessRemoteUrls: false` are required together.** The page is
  `file://` and must reach the book file, and must not reach the network.
- **The web view must hold OS focus.** Arrow-key page turns are handled inside
  the page, so focus is forced on `LoadSucceededStatus`; without it the first
  arrow press after opening a book goes nowhere and reads as broken.

**Reader assets load from disk, not `qrc:`.** A `qrc:` page cannot read book
files from the filesystem. They install to `/usr/share/omabook/reader` and
resolve at runtime through a four-tier search: `$OMABOOK_READER_DIR`, then
`/usr/share/omabook/reader`, then `{data_dir}/reader` for a per-user install,
then up to five parent directories of the executable for a source tree.

The book path and the CFI are percent-encoded into the reader URL's query
string, keeping only `A-Za-z0-9-_.~` literal. CFIs are full of `()!/` and book
filenames are routinely non-ASCII; an unencoded `&` or space truncates the URL.

**Highlights and notes.** Selecting text raises a small toolbar offering
*Highlight* and *Note*. One table holds both — **a highlight is a note with a
quote and no body**. Both anchor on a CFI, are unique per (book, CFI) so
highlighting twice updates rather than duplicates, and re-highlighting never
erases a body already written. foliate paints them through `draw-annotation`,
notes in `#f5a623` and bare highlights in `#ffd54a`. Annotations are painted in
three places — bulk on connect, incrementally on save, and again on
`create-overlay`, which is what recovers a section that had not been rendered
when the bulk load ran.

**The pdf.js polyfill stays until it can be deleted.** The pdf.js inside
foliate-js calls `Map.prototype.getOrInsertComputed`, which the Chromium inside
QtWebEngine does not implement. `js-polyfills.js` supplies it as a plain script
before any module, and `pdf-worker-shim.mjs` applies it again for the worker,
which inherits nothing from the page, before importing the real worker. Both
live outside the vendored tree so a `git pull` of foliate-js cannot undo them.
Delete both once the engine ships the methods.

### 5.4 TTS with autoplay

**Removed on this branch.** It lives on `main-with-ai-features`, unchanged and
working; the section number is kept so that the `SPEC §x` references in code
and in the other branch's history still resolve.

For the record, what went: the page-by-page read-aloud loop with automatic page
turns, its measured chunk sizes, the generation counter that stopped an
abandoned session's audio resurfacing, and the Kokoro and `qt6-speech` backends
behind them. `qt6-multimedia`, `qt6-multimedia-ffmpeg` and `qt6-speech` left the
dependency list with it.

### 5.5 What AI work may run unattended

**Removed on this branch.** It lives on `main-with-ai-features`, unchanged and
working; the section number is kept so that the `SPEC §x` references in code
and in the other branch's history still resolve.

The rule it enforced — *background work never uses a remote provider* — has no
subject here: nothing runs in the background and nothing is remote. It is worth
knowing it existed, because it is the reason the AC-power detection in
`core/ai/power.cpp` existed too, and why both are gone rather than one.

### 5.6 Page summarization

**Removed on this branch.** It lives on `main-with-ai-features`, unchanged and
working; the section number is kept so that the `SPEC §x` references in code
and in the other branch's history still resolve.

### 5.7 Q&A about the book

**Removed on this branch.** It lives on `main-with-ai-features`, unchanged and
working; the section number is kept so that the `SPEC §x` references in code
and in the other branch's history still resolve.

The `book_chunks` table and its `ordinal` column existed to make the `so_far`
scope spoiler-free. Migration 006 drops them (§4).

### 5.8 Q&A about the library

**Removed on this branch.** It lives on `main-with-ai-features`, unchanged and
working; the section number is kept so that the `SPEC §x` references in code
and in the other branch's history still resolve.

`book_embeddings` and the brute-force cosine over it went with this. Library
search is unaffected: it was always FTS5 and filters, and semantic ranking in
the search box was never built (§5.1).

### 5.9 Progress and completion

One `reading_progress` row per book, written on relocate, debounced. Fractions
are clamped rather than trusted. `unread → reading` on first save, `→ finished`
at 0.98 or by explicit "Mark as finished".

### 5.10 Reading queue

An ordered list, drag to reorder, renumbered gaplessly from 1 on save. Removal is
soft so "finished from the queue" is distinguishable from "changed my mind".
Reordering only acts on the unfiltered, unsearched Queue view — every other list
is database-ordered and a drag there would be silently undone on reload — and
uses row-move semantics rather than a model reset so the drag animation survives.

### 5.11 Encrypted backups to R2

**Layer 1 ships:** `VACUUM INTO` on clean exit and as a
pre-backup hook, so restic captures a restorable database rather than a torn
one; exclude patterns for regenerable data. **Layer 2 — `omabook backup`
wrapping restic — is deferred.** A restore path must be executed end-to-end at
least once or the feature does not exist.

### 5.12 QML wiring

- **Bridge objects are registered QML types, not context properties.** They go
  into the module `com.omabook.app` with `qmlRegisterType`, and the QML
  instantiates them (`LibraryModel { id: library }`). That is what lets
  `AiController` and `TtsController` each exist twice with independent state
  (§5.13), which a context property could not do.
- **`reader.qrc` embeds the brand mark and nothing else.** The reader page loads
  from disk (§5.3), so nothing else under `assets/reader/` belongs in a qrc —
  and in particular not `spike.html`.
- **The `--probe-*` / `--verify-reader` / `--headless-check` argument harness**
  keeps its `PROBE-*:` and `VERIFY *:` log prefixes and its exit codes. It is
  how the app is smoke-tested end to end without a screenshot, and external
  tooling greps for those exact strings.
- **Percent-encoding goes through `QUrl::toPercentEncoding`**, which keeps
  exactly `A-Za-z0-9-_.~` literal and hex-encodes every other byte, UTF-8 paths
  included. Verified, not assumed, and not hand-rolled.
- **Argument handling stays in QML.** `Qt.application.arguments` is where
  `--show`, `--open` and the probe harness are read. What is missing is a
  handler for a *bare file path*: the `.desktop` carries `Exec=omabook %f` and
  nothing reads it, so double-clicking a book opens the library rather than the
  book. That belongs in `main()`, and it is listed in TODO.md.
- **Property names are `snake_case`; invokables are `camelCase`.** The QML reads
  `bridgeObject.last_cfi` and `library.status_line`, but calls
  `library.setFilterAndReload(...)`. The QML is the contract; `Q_PROPERTY` lets
  the exposed token and the C++ accessors differ, so the C++ side stays
  idiomatic. A camelCased property token here is a binding that silently reads
  `undefined`.
- **The two singletons need an explicit import.** `Theme.qml` and `Icons.qml`
  are `pragma Singleton`. A flat `qrc:` resolves ordinary sibling components
  implicitly but **not** singletons, so each is registered with
  `qmlRegisterSingletonType(QUrl("qrc:/Theme.qml"), "omabook", 1, 0, "Theme")`
  and every file that uses one carries an `import omabook` line. Getting it
  wrong presents as `Theme is not defined` at first instantiation, not at build
  time.

### 5.13 Behaviours that are easy to lose

Recorded because each is deliberate and none is self-evident:

- **Result properties are written before the busy flag is cleared**, everywhere.
  A handler on `busyChanged` reading the results in the same tick would
  otherwise see the previous operation's values.
- **A signal is not named `pageChanged`** where a property named `page` exists,
  because the property's generated notifier already claims that name and the
  overload is ambiguous to `moc`. The same reason renames the setters
  `applyMode`, `changeSpeed` and `changeVoice`.
- **The Omarchy theme reader lives in the core, not the app.** It is pure
  QtCore, and in core it is reachable by the test binary — which matters,
  because the watcher's behaviour across a real theme swap is exactly the thing
  worth a regression test.
- **The Omarchy theme watcher watches the *parent* directory.**
  `omarchy-theme-set` deletes `current/theme/` and renames a new one into place,
  so a watch on `theme/` follows the deleted inode into the void and never fires
  again. Watching `current/` survives the swap. A 150 ms settle window coalesces
  the burst of events one theme change produces; without it the palette is read
  mid-swap and the fallback colours flash across the window.
- **The theme refresh writes only the fields that actually changed**, or every
  touch of the theme directory relayouts the window.
- **Settings precedence is stored setting, then environment variable, then
  built-in default** — not the reverse. A choice made in Settings is the more
  deliberate of the two, and it would be baffling for an exported variable to
  quietly override the picker that claims to control it. The remote API key
  follows the same order and its value is never exposed back to QML, only
  `hasRemoteKey`.
- **A book row is inserted idempotently on `file_hash`**, returning the existing
  id. Import is therefore safe to re-run, and a fixed extractor does *not*
  retroactively improve existing rows: they must be deliberately refreshed.

---

## 6. Process model, services, and the deferred server

### 6.1 No daemon, by construction

Starts when launched, exits when closed. No server, no socket, no session, no
login.

### 6.2 Local AI services

**Removed on this branch.** There are no services, local or otherwise: no
Ollama, no Kokoro container, no `compose.yml`. Nothing this application does
requires anything else to be running, which was the direction §6.1 was already
pointing.

### 6.3 The deferred `omabook serve` mode

Cheap, for one structural reason: `src/core` knows nothing about QML. A later `omabook-serve` is a second `.pro` including
`core.pri`, serving the same vendored foliate-js over HTTP. Opt-in, off by
default, behind auth, and a separate binary so the desktop app never links a web
server.

### 6.4 Concurrency

There is no thread pool. Background work is three patterns and no more, set out
in CLAUDE.md: a worker
`QObject` moved to a `QThread` for the import pipeline, which on this branch is
the only long-running work there is; and `QtConcurrent::run` with a
`QFutureWatcher` for one-shot computation. The only channel between threads is a
queued signal-slot connection.

Two rules are explicit because nothing enforces them: **a `QSqlDatabase`
connection belongs to one thread**, so the import thread opens its own, and **a
custom type in a cross-thread signal must be `qRegisterMetaType`'d**, or the
signal is dropped with only a warning.

---

## 7. Risks

### 7.1 Losing behaviour quietly

The failure mode this app is most exposed to is not a crash but a quiet loss — a
threshold rounded, a fallback dropped, an ordering guarantee not noticed. §5.13
exists for that reason, and so does the rule that a subsystem's tests must cover
its behaviour before it is called done.

Mitigation: work subsystem by subsystem, bottom-up, with tests. A subsystem is
finished when its tests pass, not when it compiles.

### 7.2 PDF text quality

The heuristic, exactly. Sample three pages at 1/4, 1/2
and 3/4 of the book. No text at all, or a mean under 120 characters a page, is
`none` — a scan. Then `poor` if any sample looks like gibberish, or if more than
35% of lines repeat across samples (running headers). Otherwise `good`.

"Gibberish" needs at least 40 non-space characters to judge. It is true if more
than 8% of characters are in `= ~ | \ ^ ``, or if fewer than 45% of tokens of two
or more characters are at least 80% alphabetic. Calibrated against the real
library: readable books scored word-like ≥0.68 and symbol ratio ≤0.016; the one
broken book — a PDF whose text is an unmapped symbol font — scored 0.29 and 0.19.
Length alone did not catch it.

On this branch the verdict has no feature to gate, so it becomes a visible
library attribute instead: `poor` and `none` show a small badge on the book's
cover in the grid, `good` and unknown show nothing. That is worth more here than
it was before — a scanned PDF is now something you can see is a scan rather than
something the assistant quietly refused to work on. **OCR is out of scope.**

### 7.3 The WebEngine bridge

Every bridge call gets a timeout and a defined failure mode; both sides log.
The `readerReady()` handshake is not negotiable (§5.3).

### 7.4 Local LLM disappointment

**Not a risk on this branch** — there is no model. Retained as a number so §7.5
keeps its own.

### 7.5 Distribution reality

QtWebEngine's size puts default-install out of reach, and
`[omarchy]` inclusion is someone else's decision. Publish to the AUR early. The
integration conventions of §3.5 are good features regardless.

### 7.6 Private Qt headers

`QtCore/private/qzipreader_p.h` is how EPUBs are read, and qmake warns on every
build that the binary is tied to the exact Qt patch version it was compiled
against. On a rolling distribution that means a `qt6-base` update can break an
un-rebuilt binary. Acceptable because the package is built from source by
`makepkg`; it would not be for a binary shipped once. It also moved from QtGui
to QtCore during Qt 6, which is precisely the churn being described. `libzip` is
the escape hatch if it is ever withdrawn, and it is the only third-party
dependency this project would accept.

---

## 8. Errors

`src/core/result.h` holds a small `Result<T>` and an `Error` with a `Kind`
enum — `Io`, `Zip`, `Xml`, `Db`, `Net`, `Decode`, `Convert`, `Cancelled`.
`Decode` and `Convert` read alike and are not the same: the first is a stored
value this build no longer understands, the second is `ebook-convert` failing. Core functions
return `Result<T>` and propagate with early returns; the bridge layer collapses
the taxonomy into a `status` string and an early return, which is omawrite's
idiom.

`Kind::Cancelled` is load-bearing: a cancelled import is silent, a failed one is
logged per file and the import continues to the next book.

**No exceptions.** Qt's event loop is not exception-safe and a throw across a
signal-slot boundary is undefined.

---

## 9. Scope

**This branch is the full product minus everything in §5.4 to §5.8**:
import through metadata and offline classification; library browse, filter,
search, categories and tags; the reader for EPUB, PDF and converted MOBI;
progress, completion and the reading queue; highlights and notes.

**Still outstanding:** MPRIS is gone with the thing
that would have published it; live text-scale following; reader appearance
settings; online metadata providers; manual tag and category editing; packaging
and the AUR; backup layer 1; and a handler for the bare file path that
`Exec=omabook %f` passes.

**Deferred, deliberately:** `omabook backup` layer 2; `omabook serve` and the
PWA; OPDS and device sync; audiobooks; OCR.

**Not coming back here.** The assistant, summaries, question answering, reading
aloud and everything that talks to a model or a speech service live on
`main-with-ai-features`. This branch is not a build flag or a runtime toggle —
the code is gone, the dependencies are gone, and migration 006 has dropped the
tables. If a feature here starts to want a network request, that is the moment
to ask whether it belongs on the other branch instead.
