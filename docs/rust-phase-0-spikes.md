# Phase 0 spike results

Recorded 2026-08-23 on Omarchy 4.0.0.alpha, Qt 6.11.1, Rust 1.98.0.

## cxx-qt — PASSED

`cxx-qt 0.9.1` / `cxx-qt-build 0.9.1`, pinned exactly (`=0.9.1`) per SPEC §7.1.

Proven end to end: a `#[qobject]` declared in Rust is instantiated from QML, its
`#[qproperty]` is readable from QML, and a `#[qinvokable]` called from QML mutates
Rust state whose change propagates back to the QML binding.

```
qml: SPIKE read   : hello from rust
qml: SPIKE shouted: HELLO FROM RUST
```

### API notes for 0.9 (the docs and older examples disagree)

- `CxxQtBuilder::new_qml_module(QmlModule::new("uri").qml_files([...]))` — builder
  methods, not the struct-literal `QmlModule { uri, rust_files, qml_files }` form
  that older examples show. Rust sources are registered with `.file()`.
- The `self:` receiver must be written literally as `Pin<&mut T>`. A qualified
  `core::pin::Pin<&mut T>` is rejected by the macro with
  "Expected a non mutable T reference!", which is a misleading message.
- `rust_mut()` requires `use cxx_qt::CxxQtType;` in scope.

## WebEngine + QWebChannel — PASSED, with one real finding

`QtWebEngineQuick::initialize()` is not wrapped by cxx-qt-lib, so it is called
through a three-line C++ shim in `cpp/webengine_init.cpp`, compiled via
`CxxQtBuilder::cpp_file()` and invoked from `main()` before `QGuiApplication` is
constructed. This works, and it is the requirement a Quickshell plugin could
never satisfy (SPEC §3.2) — an application controls its own `main()`.

Reader assets load from `qrc:` (`reader.qrc` via `CxxQtBuilder::qrc()`), so the
binary stays self-contained.

### Finding: the QWebChannel handshake completes AFTER LoadSucceeded

The first attempt called into the page from `onLoadingChanged` /
`LoadSucceeded`. The call evaluated against an undefined function, returned
`undefined`, and **failed silently** — the callback still fired, so nothing
looked wrong. This is exactly the "dropped message stalls the autoplay loop"
risk in SPEC §7.3, and it would have been miserable to debug later.

**The rule, now baked into the design:** the page defines everything Rust may
call, *then* announces itself with `bridge.readerReady()`. Rust never calls into
the page before that signal. Verified round trip:

```
SPIKE bridge: cfi=epubcfi(/6/14!/4/2/2) fraction=0.42 text="the visible page text"
SPIKE bridge: reader announced ready
SPIKE bridge: cfi=epubcfi(/6/14!/4/2/4) fraction=0.43 text="the NEXT page text"
```

That last line is the `advanceToNextPage()` cycle the TTS autoplay loop needs:
QML → `runJavaScript` → page turns → page calls back into Rust with new text.

### Testing note

WebEngine does **not** work under `QT_QPA_PLATFORM=offscreen` — the view never
loads and the run is silent. Spikes must run on the real Wayland session.
Also redirect output to a file rather than piping: Rust's stdout is block
buffered, and a `timeout` SIGTERM discards the buffer, which makes a passing run
look like a silent failure.

## Environment gaps found

- No Rust toolchain existed. Installed via `mise` (already this machine's
  toolchain manager for ruby/node), so no sudo and no rustup: `mise use -g rust@latest`.
  Pinned in `.mise.toml`.
- `qt6-webengine`, `qt6-webchannel`, `qt6-multimedia`, `qt6-declarative` all
  already present.
- Still missing, needed later: `calibre` (MOBI import), `restic` (Phase 7),
  `ollama` is installed but the service is inactive.
- `~/Books` does not exist; there is no local corpus to run the PDF text-quality
  audit (SPEC §7.2) against yet. **That audit is still outstanding.**

## PDFs need a JS polyfill (found 2026-08-23)

Opening any PDF failed with `getOrInsertComputed is not a function`, while
EPUBs were fine. The cause is not omabook: the pdf.js bundled inside
foliate-js uses `Map.prototype.getOrInsertComputed`, a recent TC39 upsert
proposal that the Chromium inside QtWebEngine does not implement yet.

Fixed from outside the vendored tree, so `git pull` on foliate-js cannot undo
it:

- `assets/reader/js-polyfills.js` defines `getOrInsert` and
  `getOrInsertComputed` on `Map` and `WeakMap`, loaded before any module.
- pdf.js also runs a **worker**, which inherits nothing from the page, so
  `assets/reader/pdf-worker-shim.mjs` applies the same polyfill and then
  imports the real worker. `GlobalWorkerOptions.workerSrc` is repointed at the
  shim just before a PDF opens.

Delete both once the engine ships the methods.

**Testing lesson.** Every reader verification until this point used Moby-Dick,
an EPUB. Half the real library is PDFs, and none of them had ever been opened.
Format coverage belongs in the verification, not just "a book opens".
