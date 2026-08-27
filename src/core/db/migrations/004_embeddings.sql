-- Vector storage for retrieval (SPEC §4.1).
--
-- Plain BLOB columns and cosine similarity computed in Rust, rather than the
-- sqlite-vec extension. Two reasons: sqlite-vec is still alpha, and at this
-- scale an index buys nothing measurable. A personal library is hundreds of
-- books; the two per-book scopes search a few thousand vectors and the
-- library scope searches one per book. Adding an ANN index here would be
-- optimising a millisecond.
--
-- If a library ever grows enough for that to stop being true, this schema is
-- what an index would be built over.

CREATE TABLE chunk_embeddings (
    chunk_id INTEGER PRIMARY KEY REFERENCES book_chunks(id) ON DELETE CASCADE,
    book_id  INTEGER NOT NULL REFERENCES books(id) ON DELETE CASCADE,
    model    TEXT NOT NULL,
    dims     INTEGER NOT NULL,
    vector   BLOB NOT NULL          -- little-endian f32, `dims` of them
);
CREATE INDEX idx_chunk_embeddings_book ON chunk_embeddings(book_id);

-- One vector per book, over title + authors + description + category + tags.
-- This is what answers "what maths books do I have?".
CREATE TABLE book_embeddings (
    book_id INTEGER PRIMARY KEY REFERENCES books(id) ON DELETE CASCADE,
    model   TEXT NOT NULL,
    dims    INTEGER NOT NULL,
    vector  BLOB NOT NULL,
    -- What was embedded, so it can be re-embedded when the metadata changes.
    source_hash TEXT NOT NULL
);

-- How far each book has been prepared for retrieval.
ALTER TABLE books ADD COLUMN chunked_at TEXT;
