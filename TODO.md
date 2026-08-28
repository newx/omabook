# omabook — Port TODO

Derived from [SPEC.md](SPEC.md); conventions in [CLAUDE.md](CLAUDE.md). Section
references (§) point into SPEC.md.

**What this is.** A port of the working Rust implementation at
`~/Projects/omabook-rust` to C++17 and Qt 6, in omawrite's shape. The product does
not change. Phases P1–P9 reach parity with the Rust build; the work after them
is what the Rust build had not finished either.

**How to work through it.** Bottom-up, subsystem by subsystem, with the Rust
source open beside you. **A subsystem is done when its tests pass, not when it
compiles.** The Rust core carries 75 tests and they are the acceptance criteria —
each phase below names the ones it owes.

**The largest risk (§7.1)** is not that the port fails but that behaviour is
lost quietly: a threshold rounded, a fallback dropped, an ordering guarantee
unnoticed. SPEC §5.13 lists the behaviours that look like details and are not.
Read it before starting any phase.

---

## Phase S — Strip the AI and speech features — **done**

This branch is the library and reader alone. The reduction is real rather than a
switch: the code is deleted, the dependencies are gone, and the schema has been
dropped.

- [x] **Migration 006** drops `chunks_fts` and its triggers, `chunk_embeddings`,
      `book_embeddings`, `book_chunks`, `summaries`, and the `books.chunked_at`
      / `embedded_at` / `auto_tagged_at` columns. One-way by design (§4)
- [x] **The runner vacuums after applying anything.** `DROP TABLE` frees pages
      inside the file but never shrinks it; without this an indexed library
      would sit at its old size for ever. 45 MB → **212 KB** on the real one,
      with all 51 books and 6 notes intact
- [x] `src/core/ai/` and `src/core/tts/` deleted, with `services.{h,cpp}` —
      which did nothing but start Ollama and Kokoro
- [x] `AiController`, `IndexWorker`, `TtsController`, `TtsWorker` deleted
- [x] `AiPanel.qml`, `AskView.qml`, `AnswerBox.qml` deleted; `Reader.qml`,
      `Main.qml` and `Sidebar.qml` trimmed
- [x] `qt6-multimedia`, `qt6-multimedia-ffmpeg` and `qt6-speech` out of the
      `.pro` and the PKGBUILD; `compose.yml` gone
- [x] `LibraryModel::showRankedBooks` and `pdfPageText` removed — the first
      existed only to receive the Ask answer's ranked ids, the second only to
      feed PDF pages to read-aloud
- [x] **Settings rebuilt around a LIBRARY section** — the remembered import
      folder, and an Import now button. Every previous section was AI or speech
- [x] **`text_quality` becomes visible instead of vanishing.** It used to gate
      read-aloud; it is now a badge on the cover for `poor` and `none`, so a
      scanned PDF is something you can see is a scan (§7.2)
- [ ] Search is FTS5 and filters only, as it always was — confirm nothing in the
      UI still implies semantic ranking

---

## Phase P0 — Scaffolding and spikes — **done**

- [x] Repo, `git init`, MIT licence, `.gitignore`, `docs/`
- [x] `bin/build`, `bin/test`, `bin/install` in omawrite's shape
- [x] Assets and `pkgbuild/` carried over from the Rust repo
- [x] **qmake carries the full module set on Qt 6.11.2** — `webenginequick`,
      `webchannel`, `multimedia`, `texttospeech`, `sql`, `quickdialogs2`,
      `concurrent`, `core-private` all configure, compile, link, and resolve
      their QML imports at runtime. `cmake` is not installed on this machine and
      is not needed. See `docs/spikes.md`
- [x] **QSQLITE is not a cut-down SQLite.** WAL, FTS5 with
      `tokenize='unicode61 remove_diacritics 2'`, and `VACUUM INTO` all verified
      against the shipped driver (SQLite 3.53.4). No direct `sqlite3` dependency
- [x] **`QZipReader` reads real EPUBs** — `QT += core-private`,
      `#include <QtCore/private/qzipreader_p.h>`. Note it moved from QtGui to
      QtCore in Qt 6 (§7.6)
- [x] **The core/app boundary is compiler-enforced.** A `.pro` that omits `qml`
      from `QT` makes `#include <QQmlEngine>` a compile error
- [x] `omabook.pro` plus a `.pri` per subsystem, and `tests/tests.pro`. One
      test binary with one class per subsystem, since a single `tst_omabook.cpp`
      would be the file everybody edits
- [x] `bin/test` greps for the fully-qualified `#include <QtQml/...>` form that
      would slip past the module-list guard

---

## Phase P1 — Core foundation

Nothing in this phase touches Qt Quick. It is the layer everything else sits on,
and it is the cheapest place to be wrong.

- [x] **`src/core/result.h`** (§8) — `Error{Kind, message}` with
      `Io|Zip|Xml|Db|Net|Decode|Convert|Cancelled`, `Result<T>`, `Result<void>`
      and `RETURN_IF_ERR`. No exceptions
- [x] **`src/core/db/database.{h,cpp}`** ← `db/mod.rs`
  - [ ] `Database::forCurrentThread()` — named per-thread connections, the only
        sanctioned way to reach SQLite. Never store a `QSqlDatabase` in a member
  - [ ] `setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000")` before `open()`, then
        `PRAGMA journal_mode=WAL`, `foreign_keys=ON`, `synchronous=NORMAL`
  - [ ] `dataDir()` / `configDir()` / `cacheDir()` — XDG with `~/.local/share`,
        `~/.config`, `~/.cache` fallbacks
  - [ ] chmod 0600 on the db file and its `-wal` / `-shm` siblings
  - [ ] `vacuumInto(path)` — removes an existing target first (§5.11)
- [x] **`src/core/db/migrations.{h,cpp}`** ← `db/migrations.rs`
  - [ ] The five `.sql` files copied **byte-for-byte** into
        `src/core/db/migrations/` and compiled in through the resource system
  - [ ] Runner keyed on `PRAGMA user_version`, one transaction per migration,
        refusing to run against a database from a newer schema
  - [ ] **Append-only.** Never edit a shipped migration
- [x] **`src/core/models/`** ← `models/book.rs`
  - [ ] `Book`, `NewBook`; `BookStatus`, `TextQuality`, `BookFormat` as
        `Q_ENUM`s with the same lowercase string encodings, `fromString`
        returning an error rather than defaulting on an unknown value
  - [ ] `Book::readablePath()` prefers `readingPath`; `authorLine()` joins with
        `", "` and falls back to `"Unknown author"`
  - [ ] `authors` serialises to and from a **JSON array as text**
- [x] **`src/core/repo/bookrepository.{h,cpp}`** ← `repo/book_repo.rs` (889 lines,
      the biggest single file in the core)
  - [ ] `list(filter, sort)`, `find`, `findByHash`, `count`, `counts`, `listIds`
  - [ ] `insert` idempotent on `file_hash`, returning the existing id
  - [ ] `saveProgress` — clamps the fraction, moves `unread → reading` on first
        save and `→ finished` at 0.98
  - [ ] `search(query, sort)` → `(books, strategy)`. FTS5 with quoted prefix
        terms joined by implicit AND; **any** FTS error falls back to a `LIKE`
        substring match rather than surfacing (§5.1)
  - [ ] `toggleFavorite`, `toggleQueued`, `isQueued`, `reorderQueue`
        (renumbering gaplessly from 1), `delete`
  - [ ] The Queue filter overrides the requested sort with `position`
- [x] **`src/core/repo/noterepository.{h,cpp}`** ← `repo/notes.rs`
  - [ ] `upsert` matching on `(book_id, cfi)`, `COALESCE`-preserving fields left
        unset, rejecting an annotation with neither quote nor body
  - [ ] `forBook` ordered by `page_fraction` then `created_at`; `all` newest first
- [x] **`src/core/repo/taxonomy.{h,cpp}`** ← `repo/taxonomy.rs` — `slugify`,
      `ensure` (find-or-create by slug), `attach`, `listWithCounts`
- [x] **`src/core/repo/settingsrepository.{h,cpp}`** ← `repo/settings.rs`
- [x] **Tests** (30 in RepoTest, 20 in CoreTest): schema version after migrate and after migrating twice;
      downgrade refused; foreign keys enforced; idempotent insert; authors round
      trip; case-insensitive title sort; first progress moves to reading;
      threshold finishes; fraction clamped; `vacuum_into` snapshot reopens;
      empty query returns nothing; prefix match; author match; accent
      insensitivity; all terms must match; **punctuation never errors**; index
      follows updates and deletes; queue toggle survives removal; queue order
      respected and gapless; delete cascades to progress/notes/queue/chunks/
      embeddings and drops out of search; deleting a missing book is not an
      error; highlight with no body; re-highlight updates rather than
      duplicates; re-highlight preserves an existing body; notes in reading
      order; empty annotation rejected; slugs normalise; ensure idempotent
      across spellings; empty categories unlisted; counts reflect assignments
- [x] **A database written by the Rust build opens unchanged** (§4). The real
      47 MB library — 51 books, 4,755 embedded chunks — is copied to a temp dir
      and read back through `BookRepository`, so enum decoding, the JSON
      authors column and the progress join are all exercised against data the
      port did not write. Skips rather than fails where there is no library

---

## Phase P2 — Import pipeline (§5.2)

- [x] **`import/scanner.{h,cpp}`** — recursive, no symlinks, skip dotfile
      components, known extensions only, drop files under 1024 bytes, sorted by
      path. A missing root is empty, not an error
- [x] **`import/hash.{h,cpp}`** — SHA-256 (`QCryptographicHash`) over the file
      length as eight little-endian bytes then the first 1 MiB
- [x] **`import/metadata.{h,cpp}`** — `FileMetadata`, `Cover`, and the junk-title
      rules: under 3 characters; starting `pii:`, `the project gutenberg ebook #`,
      `microsoft word`, `untitled`; ending `.doc`/`.pdf`/`.indd`/`.qxd`; or fewer
      than half the characters alphabetic. Falls back to a tidied filename
- [x] **`import/epub.{h,cpp}`** ← `import/epub.rs` (535 lines)
  - [x] `QZipReader`; `container.xml` → OPF rootfile → manifest, spine, metadata
  - [x] **`QXmlStreamReader` matched on `namespaceUri()` + local name**, `href`
        and `media-type` in the empty namespace, and a
        `QXmlStreamEntityResolver` for undeclared entities (§5.2)
  - [x] Cover in priority order: EPUB 3 `properties="cover-image"`; EPUB 2
        `<meta name="cover">` resolved through the manifest; a manifest item
        whose id or href contains "cover" with an image extension
  - [x] ISBN-13 accepted only with 13 digits **and** a hint — an `isbn` scheme, a
        raw value containing "isbn", or a `97` prefix
  - [x] `resolveRelative` — hrefs are relative to the OPF's directory, joined by
        string, `.` dropped and `..` popping
  - [x] `stripMarkup` — skip `<script>`/`<style>` by depth, newline on block
        tags, unescape text
  - [x] **Caps re-checked against the actual read**, never the zip header: 12 MiB
        a cover, 32 MiB an entry, 256 MiB a book
- [x] **`import/mobi.{h,cpp}`** ← `import/mobi.rs` — PalmDB record 0, `BOOKMOBI`
      magic at 60, `MOBI` magic at record0+16, encoding 65001 → UTF-8 else
      byte-as-codepoint, EXTH block when `flags & 0x40`, record types
      100/101/103/104/105/106/503/524, a record length under 8 breaking the loop.
      All access bounds-checked; a truncated file is an error, never a crash
- [x] **`import/pdf.{h,cpp}`** — `QProcess` for `pdfinfo`, `pdftoppm -png -f 1 -l 1
      -r 80 -singlefile`, `pdftotext -f N -l M -layout … -`. **Connect
      `errorOccurred`, not only `finished`**, and give every process a timeout
  - [x] The text-quality heuristic with its exact thresholds (§7.2): three
        samples at 1/4, 1/2, 3/4; mean under 120 characters → `none`; symbol
        ratio over 0.08 or word-like ratio under 0.45 → `poor`; repeated-line
        ratio over 0.35 → `poor`; under 40 non-space characters is inconclusive
- [x] **`import/covers.{h,cpp}`** — content-addressed at
      `{data}/covers/{hash}.{ext}`, extension sanitised to five lowercase ASCII
      alphanumerics defaulting to `jpg`
  - [x] **New in the port:** thumbnail with `QImageReader`, `setAutoTransform(true)`,
        and `setScaledSize` computed from `reader.size()` to fit 320×480,
        written as JPEG quality 85 (§5.2)
- [x] **`import/classify.{h,cpp}`** ← `import/classify.rs` — the 26-shelf keyword
      table **in its exact order** (specific before general: Science Fiction
      before Fiction, Mathematics before Science), whole-word matching, BISAC and
      Library-of-Congress head extraction, `looksLikeAName` (2–3 capitalised
      words, none fully uppercase) excluding proofreader credits, and title
      casing that preserves inner capitals
- [x] **`import/pipeline.{h,cpp}`** — the chain, per-file isolation so one bad
      book does not abort a run, category from the first subdirectory below the
      scan root, subject tags capped at 6 and filtered for catalogue noise,
      and an `ImportReport` naming only what happened
- [x] **Tests** (28 in ImportTest, 25 in ParsersTest): scanner finds and ignores, recurses, skips hidden and
      truncated, survives a missing root; hash is content- not name-based,
      differs on content, differs on same-prefix-different-length, errors on a
      missing file; junk titles rejected and real ones kept; namespace prefixes
      stripped; hrefs resolved; ISBN needs digits and a hint; Dublin Core parsed;
      subjects collected; EPUB 3 and EPUB 2 covers found; **a zip entry that
      inflates past the cap is rejected**; MOBI EXTH records read with EXTH title
      winning; full name used when EXTH has none; a non-MOBI refused; a corrupt
      EXTH length cannot loop; repeated-line ratio; gibberish detection including
      **mathematical prose that is not gibberish** and symbol-font mojibake that
      is; a short fragment not judged; a missing PDF reported not crashed; cover
      extensions sanitised; folder beats everything; BISAC and LoC heads land on
      a shelf; specific shelf wins; a later subject can name the shelf; unknown
      heads pass through title-cased; catalogue noise is not a category; the
      title alone is enough; whole-word matching; a person's name is never a
      category; inner capitals survive; **a MOBI in a flat folder imports with
      title, category and tags** (the flagship end-to-end case)

---

## Phase P3 — Speech and AI core — **removed on this branch**

Ported in full, then deleted here along with the features it served (SPEC §5.4
to §5.8). It stands on `main-with-ai-features`: the Kokoro client and sentence
chunker, Ollama and Anthropic, the work policy and its AC-power detection,
vectors and cosine, the prompts, the indexer and the assistant. 53 of the
suite's tests went with it.

## Phase P4 — Application shell

The first phase with a window in it.

- [x] **`src/app/main.cpp`** — in this order and no other:
      `QtWebEngineQuick::initialize()`, then `QGuiApplication`, then
      application/organization/desktop-file names and the window icon, then
      `QQuickStyle`, then the backends, then **`setContextProperty` and
      `qmlRegisterType` before `load()`**. Connect
      `QQmlApplicationEngine::warnings` and print them
- [x] `qRegisterMetaType` for every custom type that crosses a thread
- [x] **`src/core/omarchy.{h,cpp}`** — in core rather than app, because it is
      pure QtCore and the swap behaviour below is worth a test.
      `~/.local/state/omarchy/current/theme.name`
      and `theme/colors.toml`, a permissive line-based parser reading `mode`,
      `accent`, `foreground`, `muted`, and a fallback theme so the app works on a
      non-Omarchy desktop
  - [x] **Watch `current/`, not `current/theme/`** — `omarchy-theme-set` deletes
        and renames the child, and a watch on it follows the dead inode (§5.13)
  - [x] A 150 ms settle window coalescing the burst one theme change produces
- [x] **`src/app/bridge/thememodel.{h,cpp}`** — `mode`/`dark`/`accent`/`themeName`,
      `cycleMode`, **`applyMode` (not `setMode` — the property's notifier owns
      that name)**, `followSystemTheme` started from QML rather than the
      constructor, and a refresh that writes **only the fields that changed**
- [x] **`src/app/assets.{h,cpp}`** — the four-tier reader-asset search and the
      percent-encoded reader URL (§5.3)
- [x] **`src/app/app.qrc`** — every `.qml` aliased flat to `qrc:/`, plus the
      brand mark. **Not** `spike.html` (§5.12)
- [ ] `qml/Theme.qml` and `qml/Icons.qml` registered with
      `qmlRegisterSingletonType(QUrl("qrc:/Theme.qml"), "omabook", 1, 0, "Theme")`,
      and an `import omabook` line added to every QML file that uses one. A flat
      qrc resolves ordinary siblings implicitly but not singletons (§5.12)
- [x] The window opens against the real library — 51 books, 6 notes, the
      active Omarchy theme, 7 categories and 42 tags, verified through the
      app's own `--headless-check`
- [x] **Test:** the Omarchy theme loader, and a replay of a real
      `omarchy-theme-set` swap asserting the watcher wakes once per swap, every
      swap — the check that catches a watch on the wrong directory
- [ ] **Test:** walk `:/` with `QDirIterator` and `QQmlComponent::create()` every
      `.qml` — catches a file missing from `app.qrc`, which is otherwise silent

---

## Phase P5 — Library, sidebar and notes

- [x] **`bridge/librarymodel.{h,cpp}`** — `QAbstractListModel`, roles 256–264:
      `title`, `author`, `progress`, `status`, `format`, `bookId`, `coverUrl`,
      `isFavorite`, `isQueued`. `roleNames()` is mandatory
  - [ ] Properties `count`, `statusLine`, `filter`, `search`, `searchStrategy`,
        `busy`; the filter vocabulary `all|favorites|reading|queue|completed|
        category:N|tag:N`
  - [ ] `reload`, `setFilterAndReload`, `setSearchAndReload`, `showRankedBooks`,
        `toggleFavorite`, `toggleQueued`, `deleteBook`, `saveProgress`,
        `readingPathFor`, `titleFor`, `textQualityFor`, `pdfPageText`,
        `readerUrlFor`
  - [ ] `moveQueued` using `beginMoveRows`/`endMoveRows` — **the destination
        index is bumped by one when moving down**, per Qt's semantics — and only
        while the raw Queue view is showing
  - [ ] `importDirectory` on a worker `QObject` moved to a `QThread`, with its
        own database connection, an atomic cancel flag, and progress by queued
        signal. **Results are written before `busy` is cleared** (§5.13)
  - [ ] `isQueued` computed from one set per reload, not a query per row
- [x] **`bridge/sidebarmodel.{h,cpp}`** — the five counts plus `categoriesJson`
      and `tagsJson`. Deliberately not two more list models: they are small,
      read-only and rebuilt wholesale. Build the JSON with `QJsonDocument`
- [x] **`bridge/notesmodel.{h,cpp}`** — roles 256–263: `noteId`, `bookId`,
      `bookTitle`, `quote`, `body`, `cfi`, `isHighlight`, `createdAt` (the date
      portion only). `saveAnnotation` returning the new id or 0, `setBody`,
      `remove`, `annotationsJson` skipping notes with no CFI
- [ ] Port `Main.qml`, `Sidebar.qml`, `SidebarSection.qml`, `SidebarRow.qml`,
      `BookCard.qml`, `SearchBox.qml`, `NotesView.qml`, `NoteDialog.qml`,
      `QueueHelp.qml`, `ConfirmDialog.qml`, `FlatButton.qml`, `IconButton.qml`,
      `VectorIcon.qml`, `BrandMark.qml`, `AnswerBox.qml`
- [ ] The grid, the sidebar filters, search, favourites, the queue and its drag
      reorder all work against the real database

---

## Phase P6 — The reader (§5.3, §7.3)

**Build the bridge first, not last.** This is the riskiest integration in the
project, and the Rust build's Phase 0 finding is why the handshake rule exists.

- [x] Vendor foliate-js into `assets/reader/` (gitignored; `bin/install` fetches it)
- [x] Copy `reader.html`, `js-polyfills.js`, `pdf-worker-shim.mjs` across
      **unchanged**
- [x] **`bridge/readerbridge.{h,cpp}`** — `lastCfi`, `lastFraction`, `chapter`,
      `pdfPage`, `error`, `connected`; `pageChanged`, `readerReady`,
      `readerFailed`, `pageText`, `saveHighlight`, `requestNote`; and the
      `relocated()` / `highlightRequested` / `noteRequested` signals
  - [ ] **`relocated()` is not named `pageChanged`** — the invokable owns that
        name and the overload is ambiguous to `moc`
- [ ] `Reader.qml` — `WebChannel { registeredObjects: [bridge] }`,
      `localContentCanAccessFileUrls: true` **and**
      `localContentCanAccessRemoteUrls: false`, `showScrollBars: false`,
      `javascriptCanAccessClipboard: false`, focus forced on
      `LoadSucceededStatus`, page console piped to the host
- [ ] **Everything gated on `connected`, never on `LoadSucceededStatus`**
- [ ] Every bridge call has a timeout and a defined failure mode; both sides log
- [ ] Progress saved on relocate, debounced; highlights and notes saved and
      painted; annotations repainted on `create-overlay`
- [ ] **Verified on an EPUB *and* a PDF.** Every reader check in the Rust build
      used the same EPUB until a PDF-only crash surfaced months later
- [ ] WebEngine does not run under `QT_QPA_PLATFORM=offscreen`; this is verified
      by hand on the real Wayland session, and `tst_omabook` must not link it

---

## Phase P7 — Reading aloud and the assistant — **removed on this branch**

`TtsController` and `AiController`, their worker threads, and `AiPanel.qml`,
`AskView.qml` and `AnswerBox.qml`. See `main-with-ai-features`.

## Phase P8 — Parity check

- [ ] The `--probe-*` / `--verify-reader` / `--headless-check` harness ported
      with its exact flag vocabulary, `PROBE-*:` / `VERIFY *:` log prefixes and
      exit codes (§5.12). External tooling greps for those strings
- [x] `--headless-check`, `--verify-reader` on an EPUB **and** a PDF,
      `--probe-queue` and `--probe-highlight` all pass, and `--probe-queue`
      reorders identically to the Rust build run side by side
- [x] `--probe-import` — three books into a scratch library: categories from
      their folders, Devanagari titles intact, `text_quality` assessed (the PDF
      scored `poor`), subject tags extracted, covers thumbnailed. **31
      event-loop ticks during the import**, so it genuinely runs off the UI
      thread; the Rust build ticks 7 on the same corpus
- [x] Covers are thumbnailed as intended: 320x410, 320x414, 320x425 against the
      Rust build's 398x510, 680x880 and 564x750 originals — aspect preserved,
      all re-encoded to JPEG, 392 KB down to 110 KB
- [ ] **Run the probes against a scratch `HOME`.** They write to the real
      library — the queue probe reorders your actual reading queue — and a
      probe combined with `--open` measures a screen the reader is covering
- [ ] `bin/test` green; the ported tests cover what the Rust build's 75 covered
- [ ] Open the real database written by the Rust build and read from it
- [~] Side-by-side against `~/Projects/omabook-rust`: the queue drag, the reader on
      EPUB and PDF, highlights and library Q&A all match. Still to compare:
      search, the three ask scopes, read page and auto read, theme switch
- [ ] `qmllint` against the QML, and a run with no engine warnings

---

## Phase P9 — Ship it as an `oma*` app (§3.4, §3.5)

- [ ] `omabook.desktop` with `StartupWMClass=omabook`, `Exec=omabook %f`, and the
      EPUB/MOBI/PDF mime list
- [ ] **Handle a bare file path in `main()`.** The `.desktop` passes one and
      nothing reads it, in this build or the Rust one, so double-clicking a book
      opens the library rather than the book. Import it if it is unknown, then
      open it. The probe flags need no work — `Qt.application.arguments` reads
      the same list from QML in a C++ application as it did in the Rust one
- [ ] `pkgbuild/PKGBUILD` building through `bin/build`, with
      **`qt6-multimedia-ffmpeg` and `qt6-imageformats` in `depends`** — both are
      silent-failure dependencies (§3.4)
- [ ] `check()` running `bin/test`
- [ ] The reader assets installed to `/usr/share/omabook/reader`
- [ ] Publish to the **AUR** as `omabook`
- [ ] **Backup layer 1 (§5.11)** — `VACUUM INTO` on clean exit and as a
      pre-backup hook; exclude patterns for thumbnails and extracted-text scratch;
      document how it composes with Omarchy's restic backup
- [ ] `README.md`: install, run, restore

---

## After parity — what the Rust build had not finished either

Carried across unchanged, in rough order of value.

- [ ] **Honour `omarchy display text size` / `text-scaling-factor` live** (§3.5).
      omawrite's `systemtheme.cpp` does exactly this and ports nearly verbatim —
      the cheapest remaining integration win
- [ ] Reader appearance settings: font size, theme, margins
      (`omabookSetAppearance` exists in the page; there is no UI)
- [ ] `FetchMetadata` — Open Library then Google Books, raw payloads kept in
      `books.metadata`. Also what would supply covers for the books whose files
      contain no image
- [ ] Re-read metadata for books already imported. Import is idempotent by
      content hash, so a fixed extractor does not retroactively improve existing
      rows — they must be deliberately refreshed
- [ ] Manual tag and category editing
- [ ] Arrow left/right for previous/next page
- [ ] Match the active Omarchy theme's accent beyond dark/light
- [ ] Reading statistics; smart shelves
- [ ] Propose `omabook` for the `[omarchy]` repository once it has users (§7.5)

## Deferred, deliberately (§9)

- [ ] `omabook backup` layer 2 — restic, R2, a nightly timer, and **a restore
      executed end to end at least once**
- [ ] `omabook serve`, the PWA, and the Cloudflare Tunnel (§6.3)
- [ ] `poppler-qt6` native PDF rendering, if foliate-js's pdf.js disappoints
- [ ] OPDS; Kobo / KOReader sync; audiobooks; OCR
- [ ] A `QWebEngineUrlSchemeHandler` serving book bytes straight out of the zip,
      which would let the reader assets return to `qrc:` — cleaner than the
      current disk layout, and deliberately not attempted during the port
