-- Full-text search over the library, and the highlights/notes table.

-- One table holds both, exactly as medialib learned to do it: a highlight is a
-- note with a quote and no body. Keeping them apart would mean duplicating
-- anchoring, ordering, and the reader's draw path for no gain.
DROP TABLE IF EXISTS notes;
CREATE TABLE notes (
    id            INTEGER PRIMARY KEY,
    book_id       INTEGER NOT NULL REFERENCES books(id) ON DELETE CASCADE,
    cfi           TEXT,               -- foliate's canonical fragment identifier
    quote         TEXT,               -- the selected text
    body          TEXT,               -- the user's own words; NULL for a plain highlight
    page_fraction REAL,               -- for ordering within a book
    page_label    TEXT,
    color         TEXT,
    created_at    TEXT NOT NULL,
    updated_at    TEXT NOT NULL
);
CREATE INDEX idx_notes_book_position ON notes(book_id, page_fraction, created_at);
CREATE INDEX idx_notes_created ON notes(created_at DESC);
-- One annotation per anchor, so re-highlighting the same passage updates it.
CREATE UNIQUE INDEX idx_notes_book_cfi ON notes(book_id, cfi) WHERE cfi IS NOT NULL;

-- Full-text index over the metadata a person would search by. `content=''`
-- makes it contentless: the text is not stored twice, and rows are kept in
-- step by the triggers below.
CREATE VIRTUAL TABLE books_fts USING fts5(
    title,
    authors,
    description,
    series,
    publisher,
    content='',
    tokenize='unicode61 remove_diacritics 2'
);

-- Keep the index in step with the table.
CREATE TRIGGER books_fts_insert AFTER INSERT ON books BEGIN
    INSERT INTO books_fts(rowid, title, authors, description, series, publisher)
    VALUES (new.id, new.title, new.authors, COALESCE(new.description, ''),
            COALESCE(new.series, ''), COALESCE(new.publisher, ''));
END;

CREATE TRIGGER books_fts_delete AFTER DELETE ON books BEGIN
    INSERT INTO books_fts(books_fts, rowid, title, authors, description, series, publisher)
    VALUES ('delete', old.id, old.title, old.authors, COALESCE(old.description, ''),
            COALESCE(old.series, ''), COALESCE(old.publisher, ''));
END;

CREATE TRIGGER books_fts_update AFTER UPDATE ON books BEGIN
    INSERT INTO books_fts(books_fts, rowid, title, authors, description, series, publisher)
    VALUES ('delete', old.id, old.title, old.authors, COALESCE(old.description, ''),
            COALESCE(old.series, ''), COALESCE(old.publisher, ''));
    INSERT INTO books_fts(rowid, title, authors, description, series, publisher)
    VALUES (new.id, new.title, new.authors, COALESCE(new.description, ''),
            COALESCE(new.series, ''), COALESCE(new.publisher, ''));
END;

-- Backfill anything imported before this migration.
INSERT INTO books_fts(rowid, title, authors, description, series, publisher)
SELECT id, title, authors, COALESCE(description, ''), COALESCE(series, ''),
       COALESCE(publisher, '')
FROM books;

-- Indexes the sidebar and library views actually use.
CREATE INDEX IF NOT EXISTS idx_books_added ON books(added_at DESC);
CREATE INDEX IF NOT EXISTS idx_books_title_nocase ON books(title COLLATE NOCASE);
