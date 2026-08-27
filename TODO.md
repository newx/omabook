# omabook — Port TODO

Derived from [SPEC.md](SPEC.md); conventions in [CLAUDE.md](CLAUDE.md). Section
references (§) point into SPEC.md.

**What this is.** A port of the working Rust implementation at
`~/Projects/omabook` to C++17 and Qt 6, in omawrite's shape. The product does
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
- [ ] `omabook.pro`, `src/core/core.pri`, `src/app/app.pri`, `tests/tests.pro` —
      the four build files, with empty file lists, building an empty window
- [ ] `bin/test` greps for the fully-qualified `#include <QtQml/...>` form that
      would slip past the module-list guard

---

## Phase P1 — Core foundation

Nothing in this phase touches Qt Quick. It is the layer everything else sits on,
and it is the cheapest place to be wrong.

- [ ] **`src/core/result.h`** (§8) — `Error{Kind, message}` with
      `Io|Zip|Xml|Db|Net|Convert|Cancelled`, and `Result<T>`. No exceptions
- [ ] **`src/core/db/database.{h,cpp}`** ← `db/mod.rs`
  - [ ] `Database::forCurrentThread()` — named per-thread connections, the only
        sanctioned way to reach SQLite. Never store a `QSqlDatabase` in a member
  - [ ] `setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000")` before `open()`, then
        `PRAGMA journal_mode=WAL`, `foreign_keys=ON`, `synchronous=NORMAL`
  - [ ] `dataDir()` / `configDir()` / `cacheDir()` — XDG with `~/.local/share`,
        `~/.config`, `~/.cache` fallbacks
  - [ ] chmod 0600 on the db file and its `-wal` / `-shm` siblings
  - [ ] `vacuumInto(path)` — removes an existing target first (§5.11)
- [ ] **`src/core/db/migrations.{h,cpp}`** ← `db/migrations.rs`
  - [ ] The five `.sql` files copied **byte-for-byte** into
        `src/core/db/migrations/` and compiled in through the resource system
  - [ ] Runner keyed on `PRAGMA user_version`, one transaction per migration,
        refusing to run against a database from a newer schema
  - [ ] **Append-only.** Never edit a shipped migration
- [ ] **`src/core/models/`** ← `models/book.rs`
  - [ ] `Book`, `NewBook`; `BookStatus`, `TextQuality`, `BookFormat` as
        `Q_ENUM`s with the same lowercase string encodings, `fromString`
        returning an error rather than defaulting on an unknown value
  - [ ] `Book::readablePath()` prefers `readingPath`; `authorLine()` joins with
        `", "` and falls back to `"Unknown author"`
  - [ ] `authors` serialises to and from a **JSON array as text**
- [ ] **`src/core/repo/bookrepository.{h,cpp}`** ← `repo/book_repo.rs` (889 lines,
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
- [ ] **`src/core/repo/noterepository.{h,cpp}`** ← `repo/notes.rs`
  - [ ] `upsert` matching on `(book_id, cfi)`, `COALESCE`-preserving fields left
        unset, rejecting an annotation with neither quote nor body
  - [ ] `forBook` ordered by `page_fraction` then `created_at`; `all` newest first
- [ ] **`src/core/repo/taxonomy.{h,cpp}`** ← `repo/taxonomy.rs` — `slugify`,
      `ensure` (find-or-create by slug), `attach`, `listWithCounts`
- [ ] **`src/core/repo/settingsrepository.{h,cpp}`** ← `repo/settings.rs`
- [ ] **Tests** (~30): schema version after migrate and after migrating twice;
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
- [ ] **A database written by the Rust build opens unchanged** (§4). Copy the
      real `~/.local/share/omabook/omabook.db` to a temp dir and open it

---

## Phase P2 — Import pipeline (§5.2)

- [ ] **`import/scanner.{h,cpp}`** — recursive, no symlinks, skip dotfile
      components, known extensions only, drop files under 1024 bytes, sorted by
      path. A missing root is empty, not an error
- [ ] **`import/hash.{h,cpp}`** — SHA-256 (`QCryptographicHash`) over the file
      length as eight little-endian bytes then the first 1 MiB
- [ ] **`import/metadata.{h,cpp}`** — `FileMetadata`, `Cover`, and the junk-title
      rules: under 3 characters; starting `pii:`, `the project gutenberg ebook #`,
      `microsoft word`, `untitled`; ending `.doc`/`.pdf`/`.indd`/`.qxd`; or fewer
      than half the characters alphabetic. Falls back to a tidied filename
- [ ] **`import/epub.{h,cpp}`** ← `import/epub.rs` (535 lines)
  - [ ] `QZipReader`; `container.xml` → OPF rootfile → manifest, spine, metadata
  - [ ] **`QXmlStreamReader` matched on `namespaceUri()` + local name**, `href`
        and `media-type` in the empty namespace, and a
        `QXmlStreamEntityResolver` for undeclared entities (§5.2)
  - [ ] Cover in priority order: EPUB 3 `properties="cover-image"`; EPUB 2
        `<meta name="cover">` resolved through the manifest; a manifest item
        whose id or href contains "cover" with an image extension
  - [ ] ISBN-13 accepted only with 13 digits **and** a hint — an `isbn` scheme, a
        raw value containing "isbn", or a `97` prefix
  - [ ] `resolveRelative` — hrefs are relative to the OPF's directory, joined by
        string, `.` dropped and `..` popping
  - [ ] `stripMarkup` — skip `<script>`/`<style>` by depth, newline on block
        tags, unescape text
  - [ ] **Caps re-checked against the actual read**, never the zip header: 12 MiB
        a cover, 32 MiB an entry, 256 MiB a book
- [ ] **`import/mobi.{h,cpp}`** ← `import/mobi.rs` — PalmDB record 0, `BOOKMOBI`
      magic at 60, `MOBI` magic at record0+16, encoding 65001 → UTF-8 else
      byte-as-codepoint, EXTH block when `flags & 0x40`, record types
      100/101/103/104/105/106/503/524, a record length under 8 breaking the loop.
      All access bounds-checked; a truncated file is an error, never a crash
- [ ] **`import/pdf.{h,cpp}`** — `QProcess` for `pdfinfo`, `pdftoppm -png -f 1 -l 1
      -r 80 -singlefile`, `pdftotext -f N -l M -layout … -`. **Connect
      `errorOccurred`, not only `finished`**, and give every process a timeout
  - [ ] The text-quality heuristic with its exact thresholds (§7.2): three
        samples at 1/4, 1/2, 3/4; mean under 120 characters → `none`; symbol
        ratio over 0.08 or word-like ratio under 0.45 → `poor`; repeated-line
        ratio over 0.35 → `poor`; under 40 non-space characters is inconclusive
- [ ] **`import/covers.{h,cpp}`** — content-addressed at
      `{data}/covers/{hash}.{ext}`, extension sanitised to five lowercase ASCII
      alphanumerics defaulting to `jpg`
  - [ ] **New in the port:** thumbnail with `QImageReader`, `setAutoTransform(true)`,
        and `setScaledSize` computed from `reader.size()` to fit 320×480,
        written as JPEG quality 85 (§5.2)
- [ ] **`import/classify.{h,cpp}`** ← `import/classify.rs` — the 26-shelf keyword
      table **in its exact order** (specific before general: Science Fiction
      before Fiction, Mathematics before Science), whole-word matching, BISAC and
      Library-of-Congress head extraction, `looksLikeAName` (2–3 capitalised
      words, none fully uppercase) excluding proofreader credits, and title
      casing that preserves inner capitals
- [ ] **`import/pipeline.{h,cpp}`** — the chain, per-file isolation so one bad
      book does not abort a run, category from the first subdirectory below the
      scan root, subject tags capped at 6 and filtered for catalogue noise,
      and an `ImportReport` naming only what happened
- [ ] **Tests** (~35): scanner finds and ignores, recurses, skips hidden and
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

## Phase P3 — Speech and AI core

Still no Qt Quick. This is the rest of `omabook-core`.

- [ ] **`tts/chunker.{h,cpp}`** — 220 then 600 characters, whitespace collapsed,
      sentence terminators consuming trailing quotes/brackets/spaces, word-
      boundary fallback, hard cut for a single over-long word, **splitting on
      character and never byte boundaries**
- [ ] **`tts/kokoro.{h,cpp}`** — `GET /health`, `GET /v1/audio/voices` (accepting
      both the bare-string and object response shapes),
      `POST /v1/audio/speech` with `{model, input, voice, response_format, speed}`.
      Empty text rejected before any request. Output cached to
      `{cache}/tts/{sha256(voice \0 text)[:32]}.wav`
- [ ] **`ai/vectors.{h,cpp}`** — little-endian float blobs in `QByteArray`
      (deep-copied, `memcpy`'d back out, never aliased); cosine returning 0 on
      mismatch rather than NaN; `nearestInBook` with the `ordinal <= n` bound;
      **hybrid blending at weight 0.85 with per-chunk max**; FTS rank normalised
      as `strength / (strength + 4)`; keyword terms `OR`-joined with terms of two
      characters or fewer dropped
- [ ] **`ai/power.{h,cpp}`** — `/sys/class/power_supply`, reading `type`,
      `online`, `status`, `scope` per supply. An online `Mains` supply wins on a
      first pass before any battery is consulted; `scope=Device` is a peripheral;
      a missing directory is mains
- [ ] **`ai/policy.{h,cpp}`** — `WorkPolicy::permits` in the exact six-step order
      of §5.5, defaulting to off, every refusal carrying a reason
- [ ] **`ai/prompts.{h,cpp}`** — the four templates **verbatim**, including the
      U+2014 em dash in `askBook` and the conditional context lines that leave no
      empty labels
- [ ] **`ai/ollama.{h,cpp}`** — `GET /api/tags`, `POST /api/generate`
      (`num_predict: 500`, `temperature: 0.3`, `stream: false`),
      `POST /api/embeddings`. Strip a `<think>…</think>` preamble. Empty text
      rejected before any request
- [ ] **`ai/anthropic.{h,cpp}`** — `POST /v1/messages` with `x-api-key` and
      `anthropic-version: 2023-06-01`, `max_tokens: 2048`. **`stop_reason ==
      "refusal"` on a 200 is an error.** `available()` makes no request
- [ ] **`ai/indexer.{h,cpp}`** — paragraph packing at 1200 target / 120 minimum
      with a hard split past 2×; embedding only chunks with no row yet, so a run
      resumes; a cancel predicate checked per chunk; metadata embedding skipped
      when the source hash is unchanged
- [ ] **`ai/assistant.{h,cpp}`** — three scopes with `so_far` the default for an
      unrecognised value, 8 passages, 20 library candidates, excerpts flattened
      and truncated to 160 characters, provider selection through the policy
- [ ] **`services.{h,cpp}`** — `systemctl --user start ollama.service` then
      `ollama serve` detached; `docker start omabook_kokoro` then
      `docker compose up -d kokoro`. A missing tool is reported before anything
      is run, waiting gives up rather than hanging, and every failure message
      says what to do
- [ ] **Tests** (~45): the chunker's eight cases including the multibyte and
      infinite-loop guards; Kokoro's voice-shape parsing, cache determinism, URL
      normalisation, empty-text guard and unreachable-service behaviour; vectors'
      round trip, truncated blob, identical/opposite/orthogonal scores,
      magnitude independence, mismatch safety, term filtering, rank ordering;
      power's five cases including **the wireless keyboard**; the policy's seven
      rules including **background can never reach a remote provider**; the
      prompts' context handling and the two guard phrases; Ollama's think-strip
      and locality; Anthropic's text-block joining, remoteness, and
      **availability without a request**; the indexer's packing and hashing;
      the assistant's scope default and excerpting; services' four cases

---

## Phase P4 — Application shell

The first phase with a window in it.

- [ ] **`src/app/main.cpp`** — in this order and no other:
      `QtWebEngineQuick::initialize()`, then `QGuiApplication`, then
      application/organization/desktop-file names and the window icon, then
      `QQuickStyle`, then the backends, then **`setContextProperty` and
      `qmlRegisterType` before `load()`**. Connect
      `QQmlApplicationEngine::warnings` and print them
- [ ] `qRegisterMetaType` for every custom type that crosses a thread
- [ ] **`src/app/omarchy.{h,cpp}`** — `~/.local/state/omarchy/current/theme.name`
      and `theme/colors.toml`, a permissive line-based parser reading `mode`,
      `accent`, `foreground`, `muted`, and a fallback theme so the app works on a
      non-Omarchy desktop
  - [ ] **Watch `current/`, not `current/theme/`** — `omarchy-theme-set` deletes
        and renames the child, and a watch on it follows the dead inode (§5.13)
  - [ ] A 150 ms settle window coalescing the burst one theme change produces
- [ ] **`src/app/bridge/thememodel.{h,cpp}`** — `mode`/`dark`/`accent`/`themeName`,
      `cycleMode`, **`applyMode` (not `setMode` — the property's notifier owns
      that name)**, `followSystemTheme` started from QML rather than the
      constructor, and a refresh that writes **only the fields that changed**
- [ ] **`src/app/assets.{h,cpp}`** — the four-tier reader-asset search and the
      percent-encoded reader URL (§5.3); `composeDir()` for Kokoro
- [ ] **`src/app/app.qrc`** — every `.qml` aliased flat to `qrc:/`, plus the
      brand mark. **Not** `spike.html` (§5.12)
- [ ] `qml/Theme.qml` and `qml/Icons.qml` registered with
      `qmlRegisterSingletonType(QUrl("qrc:/Theme.qml"), "omabook", 1, 0, "Theme")`,
      and an `import omabook` line added to every QML file that uses one. A flat
      qrc resolves ordinary siblings implicitly but not singletons (§5.12)
- [ ] A window opens, follows system dark/light, and survives a theme switch
- [ ] **Test:** the Omarchy theme loader against a faked `HOME`
      (`QTemporaryDir` + an RAII restorer, as omawrite's does)
- [ ] **Test:** walk `:/` with `QDirIterator` and `QQmlComponent::create()` every
      `.qml` — catches a file missing from `app.qrc`, which is otherwise silent

---

## Phase P5 — Library, sidebar and notes

- [ ] **`bridge/librarymodel.{h,cpp}`** — `QAbstractListModel`, roles 256–264:
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
- [ ] **`bridge/sidebarmodel.{h,cpp}`** — the five counts plus `categoriesJson`
      and `tagsJson`. Deliberately not two more list models: they are small,
      read-only and rebuilt wholesale. Build the JSON with `QJsonDocument`
- [ ] **`bridge/notesmodel.{h,cpp}`** — roles 256–263: `noteId`, `bookId`,
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

- [ ] Vendor foliate-js into `assets/reader/` (gitignored; `bin/install` fetches it)
- [ ] Copy `reader.html`, `js-polyfills.js`, `pdf-worker-shim.mjs` across
      **unchanged**
- [ ] **`bridge/readerbridge.{h,cpp}`** — `lastCfi`, `lastFraction`, `chapter`,
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

## Phase P7 — Reading aloud and the assistant (§5.4–5.8)

- [ ] **`bridge/ttscontroller.{h,cpp}`** — `speaking`, `continuous`, `status`,
      `engine`, `chunksLeft`, `paused`, `speed`, `voice`, `voices`; the
      `playAudio`, `speakSystem`, `needNextPage`, `finished`, `pauseRequested`
      and `stopPlayback` signals; `startReading`, `continueWithPage`,
      `chunkFinished`, `chunkFailed`, `stop`, `togglePause`, `changeSpeed`,
      `refreshEngine`, `refreshVoices`, `changeVoice`
  - [ ] Synthesis on a worker thread with its own `QNetworkAccessManager`, one
        chunk prefetched ahead
  - [ ] **A generation counter discards results from an abandoned session**
  - [ ] **`stopPlayback` is its own signal** — clearing `speaking` leaves a chunk
        already handed to the player running to its end
  - [ ] A failed synthesis re-queues its chunk to the front rather than losing it
  - [ ] Falls back to `qt6-speech` when Kokoro is absent, and **checks that the
        system engine actually has voices** (§5.4)
  - [ ] Under 20 trimmed characters is refused with "nothing readable on this page"
  - [ ] The page-exhausted → `omabookAdvance()` → re-chunk loop, with the 250 ms
        settle for PDFs
- [ ] **`bridge/aicontroller.{h,cpp}`** — the full property set including
      `indexing`/`indexDone`/`indexTotal`, `backgroundEnabled`,
      `backgroundOnBattery`, `onMains`, `starting`, `serviceMessage`,
      `localModel`, `localModels`, `remoteModel`, `hasRemoteKey`;
      `summarizePage`, `askBook`, `askLibrary`, `indexLibrary`, `indexBook`,
      `cancelIndexing`, `indexState`, the two background setters, `startOllama`,
      `startKokoro`, `refresh`, `clearAnswer`, `refreshModels`, and the model/key
      setters
  - [ ] `libraryAnswered(ids)` wired to `LibraryModel::showRankedBooks`
  - [ ] The policy re-checked **per book** during a library index, so unplugging
        mid-run is respected, and partial work is kept on cancel
  - [ ] **Settings precedence: stored setting, then environment, then default.**
        The key itself is never exposed to QML
  - [ ] **Results written before `busy` is cleared**
- [ ] `AiController` and `TtsController` instantiated per-screen, not as
      singletons (§5.13)
- [ ] Port `Reader.qml`'s TTS chrome, `AiPanel.qml`, `AskView.qml`,
      `SettingsView.qml`, `SettingsHeading.qml`, `SettingsRow.qml`
- [ ] An hour of auto-read without drift, leak, or desync between audio and the
      displayed page

---

## Phase P8 — Parity check

- [ ] The `--probe-*` / `--verify-reader` / `--headless-check` harness ported
      with its exact flag vocabulary, `PROBE-*:` / `VERIFY *:` log prefixes and
      exit codes (§5.12). External tooling greps for those strings
- [ ] Every probe passes against the real library
- [ ] `bin/test` green; the ported tests cover what the Rust build's 75 covered
- [ ] Open the real database written by the Rust build and read from it
- [ ] Side-by-side against `~/Projects/omabook`: grid, sidebar, search, queue
      drag, reader on EPUB and PDF, highlight, note, read page, auto read,
      summarize, ask (all three scopes), library ask, theme switch
- [ ] `qmllint` against the QML, and a run with no engine warnings

---

## Phase P9 — Ship it as an `oma*` app (§3.4, §3.5)

- [ ] `omabook.desktop` with `StartupWMClass=omabook`, `Exec=omabook %f`, and the
      EPUB/MOBI/PDF mime list, so double-clicking a book opens omabook
- [ ] `pkgbuild/PKGBUILD` building through `bin/build`, with
      **`qt6-multimedia-ffmpeg` and `qt6-imageformats` in `depends`** — both are
      silent-failure dependencies (§3.4)
- [ ] `check()` running `bin/test`
- [ ] The reader assets installed to `/usr/share/omabook/reader`
- [ ] Publish to the **AUR** as `omabook`
- [ ] **Backup layer 1 (§5.11)** — `VACUUM INTO` on clean exit and as a
      pre-backup hook; exclude patterns for thumbnails, TTS cache and scratch;
      document how it composes with Omarchy's restic backup
- [ ] **Log it in `~/omarchy-setup.md`** under *Applications*: what, where, why,
      how to apply and verify, how to undo — package, desktop entry, mime
      handler, any Hyprland window rule
- [ ] **Track any hand-written config in `~/.dotfiles/`** with `install.sh` link
      lines, and commit
- [ ] `README.md`: install, run, restore

---

## After parity — what the Rust build had not finished either

Carried across unchanged, in rough order of value.

- [ ] **MPRIS** with title, chapter, cover art and position, and working
      play/pause/next — this is what lights up the Omarchy bar (§3.5), and Qt has
      no MPRIS class, so it is `QtDBus` by hand
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
- [ ] `AutoTag` — one local inference per book returning tags and a category,
      with **new tags constrained to a fixed vocabulary**; medialib's
      unconstrained version fed a runaway loop
- [ ] Manual tag and category editing
- [ ] Voice picker (the service lists 60+; the fetch already exists)
- [ ] Explain-selection UI (the prompt exists)
- [ ] A settings surface for the work policy and provider choice — today
      background indexing is switched on implicitly by "Index library"
- [ ] Semantic ranking in the sidebar search box; the book-level vectors exist
- [ ] Retrieval quality: definitional questions ("what is the Pequod?") still
      answer poorly. Reranking, chunk overlap, or a larger local model
- [ ] Chapter and book summaries, generated on request and cached — deliberately
      never precomputed (§5.5)
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
