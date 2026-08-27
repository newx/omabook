# OmaBooks

A book library and reader for Omarchy, built with Qt Quick and C++. Reads EPUB,
PDF and MOBI, follows your system light/dark theme, and can read a book aloud,
summarize a page, or answer questions about your library, all locally.

> **Status: in progress.** This is a port of
> [omabook](https://github.com/newx/omabook) from Rust to C++, so that it is
> built the same way as [omawrite](https://github.com/omacom-io/omawrite) and
> the rest of the `oma*` family. The Rust version works today; this one is
> being brought to parity subsystem by subsystem. See [TODO.md](TODO.md) for
> where it has got to, [SPEC.md](SPEC.md) for what it is meant to be, and
> [CLAUDE.md](CLAUDE.md) for how it is built.

## Install

Not yet in the Omarchy Package Repository.

```bash
git clone https://github.com/newx/omabook-cplus.git omabook
cd omabook
./bin/install
```

Then press `SUPER + SPACE` and type **OmaBooks**, or run `omabook`. Double
clicking an EPUB, MOBI or PDF opens it too.

## Reading

Import a folder of books from the sidebar. Each book is read for its title,
authors, description, cover and subjects — EPUB metadata, MOBI/AZW3 headers,
PDF info fields. Tags are the subjects. The category is the folder a book sits
in if you keep a sorted library, otherwise it is worked out from the subjects
and title offline, with no model or lookup involved. Books with no signal at
all stay uncategorised rather than guessed.

| Action | How |
|---|---|
| Turn the page | click the page edges, or arrow keys |
| Highlight a passage | select text, then **Highlight** |
| Write a note | select text, then **Note** |
| Read aloud | **Read page**, or **Auto read** to keep turning pages |
| Ask about the book | the **Assistant** panel in the reader |
| Ask about the library | the **Ask** page in the sidebar |

Reading position, highlights and notes are saved as you go. Books move from
unread to reading on first open, and to finished near the end.

## Requirements

Qt 6 (`qt6-base`, `qt6-declarative`, `qt6-webengine`, `qt6-webchannel`,
`qt6-multimedia`, `qt6-multimedia-ffmpeg`, `qt6-imageformats`, `qt6-speech`,
`qt6-svg`), `poppler` for PDF text and covers, and `xdg-desktop-portal` with a
backend.

Everything below is optional. The library, reader, search, highlights and
import all work without any of it.

| For | Install | Notes |
|---|---|---|
| Reading aloud | `docker compose up -d kokoro` | Kokoro-82M, runs locally |
| Summaries and questions | `ollama` + `ollama pull llama3.2:3b nomic-embed-text` | runs locally |
| Searching inside MOBI books | `calibre` | MOBI reads and imports without it |
| Fallback voice | `speech-dispatcher` or `flite` | used when Kokoro is not running; without one, reading aloud is greyed out |

Remote models are supported for questions and summaries by setting
`ANTHROPIC_API_KEY`, but they are never used by background work: indexing is
local only, opt in, and pauses off mains power, so unattended work cannot cost
money.

## Building

Qt 6 is the only build dependency, plus a clone of foliate-js in
`assets/reader/` — `bin/install` fetches it, or by hand:

```bash
git clone https://github.com/johnfactotum/foliate-js.git assets/reader/foliate-js
```

```bash
./bin/build          # release build, into build/
./bin/build --debug  # faster to compile, slower to run
./bin/test           # the core test suite
```

The scripts exist to resolve one thing: Arch ships qmake as `qmake6` and other
distributions ship it as `qmake`.

The source is two directories. `src/core` holds the library, import pipeline,
speech and AI, and knows nothing about QML at all; `src/app` is the Qt Quick
front end on top of it. That separation is enforced by the test build's module
list rather than by convention — see [CLAUDE.md](CLAUDE.md).

## Acknowledgements

Reading is handled by [foliate-js](https://github.com/johnfactotum/foliate-js),
the engine behind the Foliate reader, which covers EPUB, MOBI, AZW3, FB2, CBZ
and PDF behind one API.

Speech uses [Kokoro-82M](https://huggingface.co/hexgrad/Kokoro-82M) through
[Kokoro-FastAPI](https://github.com/remsky/Kokoro-FastAPI). Local models run on
[Ollama](https://ollama.com).

The build and house style follow
[omawrite](https://github.com/omacom-io/omawrite) by omacom-io.

## Author

Newton Ramos Garcia, [newx.sh](https://newx.sh) ·
[@newx](https://github.com/newx)

## License

MIT. See [LICENSE](LICENSE).
