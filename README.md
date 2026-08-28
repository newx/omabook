# OmaBooks

A book library and reader for Omarchy, built with Qt Quick and C++. Reads EPUB,
PDF and MOBI, follows your system light/dark theme, and keeps your reading
position, highlights and notes as you go.

**This branch has no AI.** No assistant, no summaries, no question answering, no
reading aloud, and nothing that talks to a model or a speech service. If you
want those, they are on `main-with-ai-features`; see *Two branches* below.

> **Status: in progress.** This is a port of
> [omabook](https://github.com/newx/omabook) from Rust to C++, so that it is
> built the same way as [omawrite](https://github.com/omacom-io/omawrite) and
> the rest of the `oma*` family. The Rust version works today; this one is
> being brought to parity subsystem by subsystem. See [TODO.md](TODO.md) for
> where it has got to, [SPEC.md](SPEC.md) for what it is meant to be, and
> [CLAUDE.md](CLAUDE.md) for how it is built.

## Install

Not yet in the Omarchy Package Repository, so it is built from source. Both
paths fetch the reader engine and build it for you.

**As a system package** — the Omarchy way, and what you want on Arch. Needs
`sudo` for the final step, because that is `pacman` installing the package:

```bash
git clone https://github.com/newx/omabook-cplus.git omabook
cd omabook
./bin/install          # builds an Arch package and installs it
```

**Into your home, with no root** — for another distribution, or a machine where
you would rather not touch `/usr`:

```bash
./bin/install-local    # ~/.local/bin, ~/.local/share
./bin/uninstall-local  # reverses it; your library is left alone
```

The two can coexist; the packaged install wins if both are present.

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

Reading position, highlights and notes are saved as you go. Books move from
unread to reading on first open, and to finished near the end.

## Shortcuts

The library is navigable without the mouse.

- `s` focuses the sidebar. `↑`/`↓` move between its rows and `Enter` picks one.
- `l` focuses the book grid. The arrow keys move between covers, and then
  `o` or `Enter` opens a book, `f` favourites it, `q` puts it in the reading
  queue, and `d` or `Delete` removes it — always behind a confirmation.
- `Ctrl+F` focuses the search field.
- `Escape` leaves the search field, and closes a book you are reading.

Single-letter shortcuts do nothing while you are typing in the search field, so
looking for *Shape Up* does not send you to the sidebar on the first keystroke.

In a book, the arrow keys turn pages and `Escape` returns to the library.

## Requirements

Qt 6 (`qt6-base`, `qt6-declarative`, `qt6-webengine`, `qt6-webchannel`,
`qt6-imageformats`, `qt6-svg`), `poppler` for PDF text and covers, and
`xdg-desktop-portal` with a backend.

`calibre` is optional, for importing MOBI and AZW3 files; everything else works
without it. Nothing here reaches the network at all — the books, the database
and the covers are all local, so the app works the same with the cable out.

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

The source is two directories. `src/core` holds the library, the import
pipeline and search, and knows nothing about QML at all; `src/app` is the Qt Quick
front end on top of it. That separation is enforced by the test build's module
list rather than by convention — see [CLAUDE.md](CLAUDE.md).

## Two branches

`main-with-ai-features` is the full application: an assistant in the reader,
questions answered from the whole library, page summaries, and reading aloud
with automatic page turns.

**This branch removes all of it**, and it is a real reduction rather than a
hidden switch — around 4,200 lines of C++ and three QML screens are gone, along
with the dependencies on `qt6-multimedia`, `qt6-multimedia-ffmpeg` and
`qt6-speech`. Migration 006 drops the embedding and summary tables too, which
on a fully indexed library took the database from 45 MB to 212 KB.

The migration is one-way: a library opened here loses its embeddings, and
re-indexing on the other branch costs roughly two minutes a book. Everything
else is shared, so a book, a bookmark, a highlight or a note written on either
branch reads fine on the other.

## Acknowledgements

Reading is handled by [foliate-js](https://github.com/johnfactotum/foliate-js),
the engine behind the Foliate reader, which covers EPUB, MOBI, AZW3, FB2, CBZ
and PDF behind one API.

The build and house style follow
[omawrite](https://github.com/omacom-io/omawrite) by omacom-io.

## Author

Newton Ramos Garcia, [newx.sh](https://newx.sh) ·
[@newx](https://github.com/newx)

## License

MIT. See [LICENSE](LICENSE).
