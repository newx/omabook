# CLAUDE.md

Orientation for coding agents working in this repository. Humans want the
[README](README.md); this file covers setup and the conventions that are not
obvious from the code.

**What this is.** omabook is a book library and reader for Omarchy — EPUB, PDF
and MOBI, with reading aloud and local AI. It was written in Rust with
[cxx-qt](https://github.com/KDAB/cxx-qt) and is being reimplemented here in
plain C++17 and Qt 6, so that it is built the same way as
[omawrite](https://github.com/omacom-io/omawrite), the Omarchy Markdown editor.
The Rust original lives at `~/Projects/omabook` and is the reference for
behaviour; [SPEC.md](SPEC.md) is the reference for intent and
[TODO.md](TODO.md) tracks the port.

**omawrite is the style authority.** When this document and omawrite disagree,
omawrite wins and this document is wrong. There are exactly three deliberate
divergences, all because omabook is roughly ten times omawrite's size; each is
named and justified where it appears rather than left to be discovered.

---

## Setup

Two things are needed before a clone will run.

**1. Qt 6.** `qt6-base`, `qt6-declarative`, `qt6-webengine`, `qt6-webchannel`,
`qt6-multimedia`, `qt6-multimedia-ffmpeg`, `qt6-speech`, `qt6-imageformats`,
plus `poppler` for PDF text and covers. Arch ships qmake as `qmake6`;
`bin/build` resolves either name.

`qt6-multimedia-ffmpeg` and `qt6-imageformats` are not optional and are easy to
miss: without the first, `QMediaPlayer` plays nothing and reading aloud is
silent with no error; without the second, WebP covers — which some EPUBs use —
decode to nothing and the book gets no cover, also with no error.

**2. foliate-js.** The reader engine is not vendored and is gitignored. Without
it the app builds and the library works, but opening a book renders nothing:

```bash
git clone https://github.com/johnfactotum/foliate-js.git assets/reader/foliate-js
```

## Build, test, run

```bash
./bin/build           # release, into build/
./bin/build --debug   # into build-debug/, faster to compile
./bin/test            # QtTest over src/core, and the boundary check
./build/omabook
```

`./bin/install` builds the Arch package through `makepkg -fsi`.

The library database lives at `~/.local/share/omabook/omabook.db`, with covers
beside it. Deleting that directory resets the app.

---

## Layout

```
omabook.pro           # the only build target; includes the two file lists
src/core/core.pri     # HEADERS/SOURCES for the core
src/core/             # library, import, search, speech, AI. Never QML.
src/app/app.pri       # HEADERS/SOURCES for the front end
src/app/              # QObjects exposed to QML, main.cpp
src/app/qml/          # every .qml, aliased flat into qrc:/
tests/tests.pro       # QtTest binary; compiles src/core directly
assets/reader/        # reader.html and foliate-js, loaded from disk over file://
pkgbuild/             # PKGBUILD, .desktop, icon
```

One `.pro`, one `make`, one installed binary — `/usr/bin/omabook` — exactly as
omawrite does it. The `.pri` files exist only so the `.pro` stays a page long;
they are file lists, not sub-projects. Do not reach for `TEMPLATE = subdirs` or
a static library: it buys no safety we do not already have and it breaks the
omawrite idiom where the test project compiles the real sources directly.

`src/core` knowing nothing about QML is the one architectural rule here. It is
what keeps a headless mode cheap to add later (SPEC §6.3), and a future
`omabook serve` is then a fifteen-line `.pro` that includes `core.pri`.

**The boundary is checked by the test build, not by convention.**
`tests/tests.pro` lists only `QT += sql network concurrent testlib` — no `qml`,
no `quick`, no `webenginequick`, no `multimedia` — so qmake never adds those
include paths and `#include <QQmlEngine>` in core code is a compile error rather
than a code review comment:

```
c.cpp:1:10: fatal error: QQmlEngine: No such file or directory
```

That is the direct translation of the Rust version's crate split, where
`omabook-core/Cargo.toml` simply did not list `cxx-qt`. Two caveats worth
knowing: the app build compiles the same files *with* those paths present, so
only `bin/test` catches a violation — run it; and the fully-qualified form
`#include <QtQml/QQmlEngine>` compiles anyway because `/usr/include/qt6` is
always on the path, so `bin/test` also greps for it.

**Where the boundary sits, exactly.** `src/core` may use QtCore, QtSql,
QtNetwork, QtConcurrent, and QtGui *for images only* — `QImage`, `QImageReader`,
`QColor`. It may not use QtQml, QtQuick, QtWebEngine, QtMultimedia, or QtWidgets.
A core class is a plain `QObject` — signals and slots are QtCore and are
encouraged — but nothing in core is written *for* QML: no `Q_INVOKABLE`, no
`qmlRegisterType`, no role enums, no `QVariantMap` shaped for a delegate. Those
belong to `src/app`.

**Divergence one: subdirectories.** omawrite puts every `.cpp`, `.h` and `.qml`
in one `src/`. At this size that would be ninety files in one folder, so core is
grouped by subsystem (`db/`, `models/`, `repo/`, `import/`, `ai/`, `tts/`). QML
stays in one directory, `src/app/qml/`, and every file is aliased to the `qrc:/`
root so components remain siblings and resolve each other through the implicit
directory import without any `import` statement.

---

## C++ conventions

These are read off omawrite's `backend.cpp` and `systemtheme.cpp`. Match them.

**A QObject exposed to QML looks like this.** Properties are read-only from
QML's side unless there is a reason otherwise, getters are trivial and inline in
the header, and the setter is private, early-returns when nothing changed, then
emits:

```cpp
class ImportController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit ImportController(QObject *parent = nullptr);

    bool running() const { return m_running; }
    QString status() const { return m_status; }

    Q_INVOKABLE void scan(const QUrl &directory);

signals:
    void runningChanged();
    void statusChanged();

private:
    void setStatus(const QString &status);

    bool m_running = false;
    QString m_status;
};
```

```cpp
void ImportController::setStatus(const QString &status) {
    if (m_status == status)
        return;

    m_status = status;
    emit statusChanged();
}
```

**Members are `m_`-prefixed and initialised at their declaration**, not in a
constructor initialiser list, unless the value depends on a constructor
argument.

**`QStringLiteral` for every string literal that becomes a `QString`.** It builds
the string data at compile time; a bare `"..."` allocates and converts on every
call. `QLatin1Char('=')` for single characters, and `QLatin1StringView` for
comparisons that never need a `QString`.

**Braces follow K&R, four-space indent, 100 columns.** Single-statement `if`
bodies drop the braces and go on their own line, as omawrite does:

```cpp
if (!url.isLocalFile()) {
    setStatus(QStringLiteral("Only local files can be opened."));
    return;
}

if (m_highlighter)
    m_highlighter->setSearch(query, currentMatchStart);
```

**Early return over nesting.** Validate, fail, return; the happy path stays at
one indent level. omawrite's `saveTo()` is the model: every failure exits before
any work happens.

**Pure logic goes in `static` member functions taking and returning values.**
omawrite's `Backend::countWords`, `normalizedLinkUrl` and `suggestedFileName` are
static precisely so the tests can call them without an application, a window, or
a file. Anything with a threshold, a heuristic, or a parse in it should be
reachable that way — the text-quality classifier, the TTS chunker, the FTS query
builder, the offline category rules, cosine similarity. If a test needs a
`QQmlEngine` to reach your logic, the logic is in the wrong place.

**`QPointer` for any QObject you did not create**, and for anything QML might
outlive you with. omawrite holds `QPointer<QTextDocument>` and
`QPointer<QWindow>` for exactly this reason.

**Prefer value types and `const` references.** `const QString &`, `const QUrl &`.
Return by value; Qt containers are implicitly shared, so returning a `QList` is
cheap.

**`QSaveFile` for every write that matters**, never `QFile` plus `write`. It
writes to a temporary and atomically renames on `commit()`, so a crash mid-write
leaves the original intact.

**Comments say why, not what.** The existing ones record the reasoning behind a
choice, often including what was tried first and why it failed. omawrite's
comment above its `QFileSystemWatcher` re-arm — *"Atomic saves can replace the
watched inode. Re-arm the watcher, but do not report our own save as an outside
edit."* — is the target register. Several comments reference `SPEC §x`; keep
that.

**No third-party C++ libraries.** omawrite has none, and the port has none.
`poppler` and `calibre` are used as external *processes*, not linked libraries.
If you find yourself wanting a library, say so in TODO.md and argue for it; do
not add it quietly. The one thing that looks like an exception is
`QtCore/private/qzipreader_p.h` — see Traps.

---

## Threading

The Rust version owned a tokio runtime and marshalled results back to the Qt
thread. In C++ the equivalent is Qt's own, and there are three patterns used in
three distinct places. Do not invent a fourth.

**The universal rule: a cross-thread signal-to-slot connection is automatically
queued, and that is the only channel between threads.** No shared mutable state,
no mutexes around app data, no direct calls. `QString`, `QByteArray` and the
primitives marshal for free; a custom struct in a signal must be a `Q_GADGET`
and `qRegisterMetaType`'d once in `main()`, because a missing registration
produces a runtime warning and a **silently dropped signal** — the nastiest bug
class in this codebase.

**1. Long, stateful, cancellable work: a worker `QObject` moved to a `QThread`.**
The import pipeline and the TTS synthesis loop are this, and nothing else is. The
worker owns its own database connection and its own `QNetworkAccessManager`,
runs the chain linearly — a direct transliteration of the Rust code, not an async
rewrite — and reports through signals:

```cpp
auto *thread = new QThread(this);
auto *worker = new ImportWorker;              // no parent, or moveToThread
worker->moveToThread(thread);                 // silently does nothing
connect(thread, &QThread::started, worker, &ImportWorker::run);
connect(worker, &ImportWorker::progress, this, &ImportController::onProgress);
connect(worker, &ImportWorker::finished, thread, &QThread::quit);
connect(thread, &QThread::finished, worker, &QObject::deleteLater);
thread->start();
```

Cancellation is a `std::atomic_bool` the worker checks between items and between
stages, written directly from the GUI thread. `QThreadPool` and
`QtConcurrent::run` are the wrong shape here: a pool thread has no identity, so
it cannot own a connection or a session.

**2. One-shot pure computation: `QtConcurrent::run` plus `QFutureWatcher`.**
Hashing a file, thumbnailing a cover, a brute-force similarity sweep.
`QFutureWatcher::finished` fires on the GUI thread.

**3. Network from the GUI thread: `QNetworkAccessManager`'s own signals.** QNAM
is already asynchronous, so a user-initiated Ollama or Anthropic call needs no
thread at all — `post()` plus a `finished` lambda. Inside a worker thread, where
the code is linear, create a QNAM *in that thread* and block on a local
`QEventLoop`; that is legitimate off the GUI thread and keeps the pipeline
readable.

**Never touch a QObject from the wrong thread.** `QMetaObject::invokeMethod(obj,
..., Qt::QueuedConnection)` is the escape hatch when there is no signal to hand.

**A `QSqlDatabase` connection belongs to one thread.** This is the single easiest
way to corrupt this app. Every thread that touches SQLite opens its own *named*
connection inside that thread and closes it before the thread ends. WAL gives
concurrent readers and one writer for free, so the design is: the GUI thread
reads, the import worker is the only writer, and the GUI refreshes its models
from the worker's signals. `Database::forCurrentThread()` in `src/core/db` makes
this the path of least resistance — use it, and never store a `QSqlDatabase` in
a member.

---

## Database

`QSqlDatabase` with the `QSQLITE` driver — no direct `sqlite3` dependency. This
was verified rather than assumed (`docs/spikes.md`): on Arch the driver links the
system SQLite, 3.53.4, and supports WAL, FTS5 with `remove_diacritics 2`, and
`VACUUM INTO`, which is everything the Rust build used `rusqlite` for.

**Opening a connection.** `setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"))`
*before* `open()`, then `PRAGMA journal_mode=WAL` and `PRAGMA foreign_keys=ON`
after. A writer transaction is `BEGIN IMMEDIATE`, not `db.transaction()`, so a
deferred lock upgrade cannot surprise you with `SQLITE_BUSY` halfway through.

**Migrations are append-only.** Add to `MIGRATIONS` in
`src/core/db/migrations.cpp`; never edit a shipped one, because a user's database
has already run it. The SQL lives in `.sql` files under
`src/core/db/migrations/` and is compiled in through the resource system, so a
migration is a new file plus one line.

**Every query is prepared and bound.** `QSqlQuery::prepare` then `bindValue`;
never concatenate a value into SQL. A query used in a loop is prepared once and
kept as a member — re-preparing per call is the classic QSQLITE performance
mistake. The one place a query is assembled as text is the FTS5 match
expression, where the terms are quoted and escaped by hand; that function is
`static` and tested.

**Scope `QSqlQuery` objects in braces** so they are destroyed before
`QSqlDatabase::removeDatabase`, which otherwise warns and leaks the handle.

**Vectors are `QByteArray` blobs of little-endian `float`.** 768 dimensions,
3072 bytes. Bind the `QByteArray` directly; construct it as a deep copy, never
`fromRawData` over a vector that will go out of scope, and `memcpy` back out
rather than aliasing, because the blob's alignment is not guaranteed. Store each
vector's norm in its own column at embed time so query time is a dot product.
The Rust version chose brute-force cosine over an ANN index because the corpus
is a few hundred thousand chunks at most (SPEC §4.1); keep that. If it ever
feels slow, `QtConcurrent::blockingMapped` over shards is the first move, not
intrinsics.

---

## Error handling

Rust's `Result<T, E>` has no cheap C++ equivalent, `std::expected` is C++23 and
we are on C++17, and Qt's event loop is explicitly not exception-safe — a throw
crossing `exec()` or a signal-slot boundary is undefined behaviour. So:

**No exceptions.** Not thrown, not caught, no `try` blocks.

**Divergence two: `src/core` carries a real error taxonomy.** omawrite sets a
status string and returns; that is right for one file and one editor, and wrong
for an import pipeline where "the file was not found", "Calibre is missing" and
"the user cancelled" must be told apart. `src/core/result.h` holds about fifty
lines:

```cpp
struct Error {
    enum class Kind { Io, Zip, Xml, Db, Net, Convert, Cancelled };
    Kind kind;
    QString message;    // shown to the user, so write it as a sentence
};

template <typename T> class Result { /* ok() / err() / isOk() / value() / error() */ };
```

Core functions return `Result<Book>`, `Result<QString>`, and propagate with
early returns. `Kind::Cancelled` is load-bearing: a cancelled import is silent,
a failed one is logged per file and the import continues.

**At the QML bridge the taxonomy collapses to omawrite's idiom.** `Kind` picks
the phrasing, `setStatus(...)` shows it, and the function returns early. The
user gets a sentence, the UI stays alive, nothing unwinds.

**Every failure is visible somewhere.** A silent `return` with no status set is
a bug — it is exactly how the Rust version's WebChannel handshake fault hid for
a day (`docs/spikes.md`). If there is genuinely nothing to show the user,
`qWarning()` it.

---

## QML conventions

**Every `.qml` file must be listed in `src/app/app.qrc`.** An unregistered file
is not a compile error and not a startup error: the type silently does not
exist, and you find out when that screen is first instantiated and renders an
empty pane. This is the single most common way to lose an hour here, and it is
the same trap the Rust build had with `build.rs`. `tst_omabook` walks `:/` with
`QDirIterator` and `QQmlComponent::create()`s every `.qml`, which catches both a
missing entry and about half of all binding typos, offscreen.

**Print QML warnings.** `main.cpp` connects `QQmlApplicationEngine::warnings`
and prints them, as omawrite does. Without it a run that prints nothing proves
nothing.

**Sizes are written at text scale 1 and scaled at use.** omawrite's rule: a
`readonly property real textScale` comes from the backend, and every hardcoded
pixel goes through `scaledSize(20)`. The desktop's text size knob
(`omarchy display text size`, GNOME's `text-scaling-factor`) must re-flow the app
live, with no restart.

**Divergence three: a `Theme` singleton.** omawrite passes `darkMode`,
`textColor` and `textScale` down into each leaf component as properties. It has
six QML files and can afford that; omabook has twenty-odd and cannot.
`qml/Theme.qml` is a `pragma Singleton` holding the palette and text scale, fed
from `ThemeBridge`. Leaf components still declare overridable properties
defaulting to `Theme.x`, so they stay previewable in isolation and the omawrite
shape survives one level down.

**JavaScript in QML is glue, not logic.** Signal handlers, layout expressions,
small pure helpers on the root object. Anything with a rule in it belongs in C++
where it can be tested. The reader page is the deliberate exception, below.

**`QAbstractListModel` subclasses live on the GUI thread, full stop.** The
import worker never touches a model; it emits row data and a GUI slot does the
insert. Override `roleNames()` — without it every role reads as `undefined` in
the delegate and nothing tells you why. Bracket every structural change with
`beginInsertRows`/`endInsertRows` and friends; emitting `dataChanged` for a
row-count change corrupts the view. A full re-query is
`beginResetModel`/`endResetModel`, which is correct and simplest.

**A multi-word property exposed to QML is `snake_case`; an invokable is
`camelCase`.** `theme_name`, `last_cfi`, `pdf_page`, `status_line`,
`categories_json`, `available_providers` — but `setFilterAndReload`,
`readerUrlFor`, `startReading`. This is not a style choice: cxx-qt never
camelCased a `#[qproperty]` name while every invokable carried an explicit
`#[cxx_name]`, so the ported QML reads exactly these tokens and the QML is the
contract. `Q_PROPERTY` lets the token and the accessors differ, so the C++ side
stays idiomatic: `Q_PROPERTY(QString theme_name READ themeName NOTIFY
themeNameChanged)`. Get it wrong and the binding reads `undefined` with no
warning at any stage.

**Do not return `QObject *` from a `Q_INVOKABLE`** unless you have called
`QQmlEngine::setObjectOwnership(obj, QQmlEngine::CppOwnership)` — otherwise the
QML garbage collector takes ownership and deletes your backend out from under
you at an unpredictable moment. Return value types or `QVariantMap`.

---

## The reader bridge

The reader is the one place where two worlds meet, and it is the riskiest part
of the app. The rules below are not style preferences; each is a bug that was
already paid for once.

**`main()` has a fixed order and there is no latitude in it:**

```cpp
int main(int argc, char *argv[]) {
    QtWebEngineQuick::initialize();                 // 1. before the app object
    QGuiApplication app(argc, argv);                // 2.
    // ... construct backends ...
    engine.rootContext()->setContextProperty(QStringLiteral("library"), &library);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));   // 3. context first, then load
}
```

Initialising WebEngine after the application object aborts at runtime. Setting a
context property after `load()` leaves the binding undefined with no error. If a
custom URL scheme is ever registered, that goes before *everything*, including
`initialize()`.

**The page announces itself; the host never calls in first.** The QWebChannel
handshake completes *after* `LoadSucceeded`, so a `runJavaScript` issued from
`onLoadingChanged` evaluates against an undefined function, returns `undefined`,
and fails silently — the callback still fires, so nothing looks wrong. The page
defines everything the host may call, *then* calls `bridge.readerReady()`.
Nothing is sent into the page before that signal arrives.

**Every bridge call has a timeout and a defined failure mode**, and logs on both
sides. A dropped message stalls the read-aloud loop, and a stall with no log is
unfindable.

**Only QVariant-representable types cross the channel.** No `QObject *` graphs.

**The page owns rendering, pagination, CFI and visible-range text extraction.
C++ owns everything else.** C++ never guesses what is on screen; it asks. That is
what keeps the backend format-agnostic, and it is why there is one reader for
EPUB, PDF and converted MOBI rather than three.

**Reader assets load from disk, not `qrc:`.** A `qrc:` page cannot read book
files from the filesystem. They install to `/usr/share/omabook/reader` and
resolve at runtime through `src/app/assets.cpp`, which also finds them in the
source tree during development. (A `QWebEngineUrlSchemeHandler` serving book
bytes straight out of the zip would be cleaner and would let the assets return
to `qrc:`; it is recorded in TODO.md as a refinement, deliberately not attempted
during the port.)

**Reader chrome is QML around the web view, never HTML inside it**, so it looks
and behaves native.

---

## External processes

`pdftotext`, `pdfinfo`, `pdftoppm` and `ebook-convert` are run with `QProcess`,
and every one of them connects `errorOccurred`: **a missing binary emits that
signal, not `finished`**, so a `finished`-only handler waits forever for Calibre
on a machine that does not have it. Read stdout incrementally for large PDFs, or
the pipe fills and the child stalls. Give every process a timeout.

---

## Testing

`tests/tests.pro` compiles `src/core` straight into a `QtTest` binary, the way
omawrite's does. Tests are `private slots` on one `QObject`, using `QCOMPARE`
and `QVERIFY`, and run under `QT_QPA_PLATFORM=offscreen`.

**Test the static helpers.** That is what they are for. The Rust version had 75
tests over the core; the port should reach the same coverage of the same
behaviour, and TODO.md tracks them per subsystem.

**Temporary state, never the real database.** `QTemporaryDir`, and
`QStandardPaths::setTestModeEnabled(true)` in `initTestCase` so nothing writes to
`~/.local/share/omabook`. omawrite's `HomeRestorer` struct — an RAII guard that
puts `HOME` back in its destructor — is the pattern for tests that need to fake
a home directory, and the theme loader needs exactly that.

**WebEngine cannot be tested here.** It does not run under the offscreen
platform — the view never loads and the run is silent rather than failing.
Reader verification is manual, on the real Wayland session, and must cover an
EPUB *and* a PDF: every reader check in the Rust version used the same EPUB
until a PDF-only crash surfaced months later.

---

## Optional services

Everything below degrades cleanly. The library, reader, search, highlights and
import never depend on any of it, and the app must never show a modal because a
service is missing — Settings shows status, and the feature's button is hidden or
disabled.

```bash
docker compose up -d kokoro                # speech; falls back to qt6-speech
ollama pull llama3.2:3b nomic-embed-text   # summaries, questions, embeddings
```

Background AI work is local only, opt in, and pauses off mains power, so
unattended work cannot cost money. Keep it that way — the rule lives in one
place, `core/ai/policy.cpp`, not at each call site, and every refusal carries a
reason the UI can show.

---

## Traps

The short list of things that will cost you an hour each.

- **An unregistered `.qml` file fails silently.** `app.qrc`. Every time.
- **A `pragma Singleton` is not resolved by the implicit directory import.**
  Ordinary sibling components are; singletons need `qmlRegisterSingletonType`
  and an `import omabook` line in every file that uses them. The symptom is
  `Theme is not defined` when a screen is first instantiated.
- **`QSqlDatabase` is per-thread.** Corruption, not an error message.
- **A custom type in a signal needs `qRegisterMetaType`**, or the cross-thread
  connection drops the signal with only a warning.
- **`moveToThread` on an object that has a parent does nothing**, and warns.
- **`QtWebEngineQuick::initialize()` before `QGuiApplication`.** Abort otherwise.
- **Do not call into the page before `readerReady()`.** Silent no-op otherwise.
- **`QProcess::errorOccurred`, not just `finished`**, or a missing `ebook-convert`
  hangs the import.
- **`QNetworkAccessManager` has no default timeout.** Set
  `setTransferTimeout` on every request, or a wedged Ollama hangs forever. If a
  localhost request stalls at connect, turn off HTTP/2 for it
  (`QNetworkRequest::Http2AllowedAttribute`).
- **`QXmlStreamReader` is strict where quick-xml was not.** Match on
  `namespaceUri()` plus local name, never `qualifiedName()`, because real OPFs
  come as `<package>`, `<opf:package>` and worse; look up `href` and `media-type`
  in the *empty* namespace; and install a `QXmlStreamEntityResolver` returning a
  space for undeclared entities, because sloppy converters emit `&nbsp;` in
  metadata and that is a hard parse error.
- **`QImageReader::setScaledSize` does not preserve aspect ratio.** Compute the
  target from `reader.size()` — which is header-only and free — and set
  `setAutoTransform(true)` or EXIF-rotated covers come out sideways.
- **`qmake` does not track new files.** Adding a source to a `.pri` needs qmake
  re-run; `bin/build` does it every time, `make` alone does not.
- **Private Qt headers pin the binary to the Qt patch version.**
  `QtCore/private/qzipreader_p.h` is how EPUBs are read — note `core-private`,
  not `gui-private`, it moved in Qt 6 — and qmake warns about it on every build.
  It is stored-and-deflate only with no zip64, which is exactly what EPUB
  permits. Rebuild after a `qt6-base` update; `libzip` is the escape hatch if it
  is ever removed.
- **Qt logs to journald, not stderr, when there is no tty.** Arch builds
  `qt6-base` with journald support, and Qt then routes *everything* —
  `qDebug`, `qWarning`, `qCritical` — to the journal whenever stderr is not a
  terminal. Run the app from a script, a pipe, or an agent and it prints
  absolutely nothing, including the message that would have told you why it
  exited. Export **`QT_FORCE_STDERR_LOGGING=1`** for any run whose output you
  intend to read, or read it back with `journalctl --user`. This is the single
  most misleading thing in this environment: silence is not success, and it is
  not failure either.
- **Redirect spike output to a file rather than piping it.** stdout is block
  buffered, and a `timeout` SIGTERM discards the buffer.

---

## Commits

**One change each**, with a message explaining the problem the change solves
rather than listing the diff. During the port, a commit is usually one subsystem
moved across with its tests passing.
