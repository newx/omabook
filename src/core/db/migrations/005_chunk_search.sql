-- Keyword index over chunks, to complement vector search.
--
-- Vector search alone retrieves passages that are *about* a subject, which is
-- what you want for "what does Ishmael say when he feels grim". It is weaker
-- for questions naming a thing — "what is the Pequod?" — where the useful
-- passages are the ones containing the literal word. Keyword search is exactly
-- the opposite, so the two are blended (see ai::vectors::hybrid_in_book).

CREATE VIRTUAL TABLE chunks_fts USING fts5(
    text,
    content='book_chunks',
    content_rowid='id',
    tokenize='unicode61 remove_diacritics 2'
);

CREATE TRIGGER chunks_fts_insert AFTER INSERT ON book_chunks BEGIN
    INSERT INTO chunks_fts(rowid, text) VALUES (new.id, new.text);
END;

CREATE TRIGGER chunks_fts_delete AFTER DELETE ON book_chunks BEGIN
    INSERT INTO chunks_fts(chunks_fts, rowid, text) VALUES ('delete', old.id, old.text);
END;

CREATE TRIGGER chunks_fts_update AFTER UPDATE ON book_chunks BEGIN
    INSERT INTO chunks_fts(chunks_fts, rowid, text) VALUES ('delete', old.id, old.text);
    INSERT INTO chunks_fts(rowid, text) VALUES (new.id, new.text);
END;

INSERT INTO chunks_fts(rowid, text) SELECT id, text FROM book_chunks;
