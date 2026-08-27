-- Initial schema. SPEC §4.
--
-- Tables for deferred features (book_chunks, vec_*, summaries, notes) are
-- created up front so Phase 6 is additive rather than a migration of live data.

CREATE TABLE categories (
    id         INTEGER PRIMARY KEY,
    name       TEXT NOT NULL,
    slug       TEXT NOT NULL UNIQUE,
    parent_id  INTEGER REFERENCES categories(id) ON DELETE SET NULL
);
CREATE INDEX idx_categories_parent ON categories(parent_id);

CREATE TABLE tags (
    id   INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    slug TEXT NOT NULL UNIQUE
);

CREATE TABLE books (
    id                  INTEGER PRIMARY KEY,
    title               TEXT NOT NULL,
    subtitle            TEXT,
    authors             TEXT NOT NULL DEFAULT '[]',   -- json array
    series              TEXT,
    series_index        REAL,

    source_path         TEXT NOT NULL UNIQUE,          -- the file as found on disk
    reading_path        TEXT,                          -- what the reader opens
    format              TEXT NOT NULL,                 -- epub|pdf|mobi|azw3|cbz
    file_hash           TEXT NOT NULL,
    file_size           INTEGER NOT NULL DEFAULT 0,
    cover_path          TEXT,

    description         TEXT,
    publisher           TEXT,
    published_date      TEXT,
    language            TEXT,
    isbn13              TEXT,
    page_count          INTEGER,
    metadata            TEXT NOT NULL DEFAULT '{}',    -- json, raw provider payloads

    category_id         INTEGER REFERENCES categories(id) ON DELETE SET NULL,
    status              TEXT NOT NULL DEFAULT 'unread', -- unread|reading|finished|abandoned
    text_quality        TEXT,                           -- good|poor|none
    rating              INTEGER,

    added_at            TEXT NOT NULL,
    started_at          TEXT,
    completed_at        TEXT,
    archived_at         TEXT,
    metadata_fetched_at TEXT,
    auto_tagged_at      TEXT,
    embedded_at         TEXT
);
CREATE UNIQUE INDEX idx_books_hash ON books(file_hash);
CREATE INDEX idx_books_status ON books(status);
CREATE INDEX idx_books_category ON books(category_id);
CREATE INDEX idx_books_archived ON books(archived_at) WHERE archived_at IS NOT NULL;

CREATE TABLE book_tags (
    book_id    INTEGER NOT NULL REFERENCES books(id) ON DELETE CASCADE,
    tag_id     INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,
    source     TEXT NOT NULL DEFAULT 'manual',  -- manual|llm
    confidence REAL,
    PRIMARY KEY (book_id, tag_id)
);
CREATE INDEX idx_book_tags_tag ON book_tags(tag_id);

CREATE TABLE reading_progress (
    book_id    INTEGER PRIMARY KEY REFERENCES books(id) ON DELETE CASCADE,
    position   TEXT,            -- CFI for reflowable, page number for fixed
    fraction   REAL NOT NULL DEFAULT 0.0,
    updated_at TEXT NOT NULL
);

CREATE TABLE queue_items (
    id             INTEGER PRIMARY KEY,
    book_id        INTEGER NOT NULL REFERENCES books(id) ON DELETE CASCADE,
    position       INTEGER NOT NULL,
    added_at       TEXT NOT NULL,
    removed_at     TEXT,
    removal_reason TEXT
);
-- One active queue entry per book; finished/removed rows stay for history.
CREATE UNIQUE INDEX idx_queue_active_book ON queue_items(book_id) WHERE removed_at IS NULL;
CREATE INDEX idx_queue_active_position ON queue_items(position) WHERE removed_at IS NULL;

-- Deferred to Phase 6, created now so it is additive. SPEC §4.
CREATE TABLE book_chunks (
    id            INTEGER PRIMARY KEY,
    book_id       INTEGER NOT NULL REFERENCES books(id) ON DELETE CASCADE,
    chapter_index INTEGER,
    chapter_title TEXT,
    ordinal       INTEGER NOT NULL,   -- monotonic within a book; powers "so far"
    cfi_start     TEXT,
    page_number   INTEGER,
    char_start    INTEGER,
    char_end      INTEGER,
    text          TEXT NOT NULL
);
CREATE INDEX idx_chunks_book_ordinal ON book_chunks(book_id, ordinal);

CREATE TABLE summaries (
    id            INTEGER PRIMARY KEY,
    book_id       INTEGER NOT NULL REFERENCES books(id) ON DELETE CASCADE,
    kind          TEXT NOT NULL,      -- page|chapter|book
    chapter_index INTEGER,
    cfi           TEXT,
    content       TEXT NOT NULL,
    model         TEXT,
    created_at    TEXT NOT NULL
);
CREATE INDEX idx_summaries_book_kind ON summaries(book_id, kind);

CREATE TABLE notes (
    id         INTEGER PRIMARY KEY,
    book_id    INTEGER NOT NULL REFERENCES books(id) ON DELETE CASCADE,
    cfi        TEXT,
    quote      TEXT,
    body       TEXT,
    created_at TEXT NOT NULL
);
CREATE INDEX idx_notes_book ON notes(book_id);

CREATE TABLE settings (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
