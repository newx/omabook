-- Remove the retrieval and summarisation schema.
--
-- This branch has no assistant, no embeddings and no read-aloud, so the tables
-- that served them are dead weight -- and not small dead weight: a fully
-- indexed library carries one 3 KB vector per chunk, which ran to tens of
-- megabytes on a corpus of fifty books.
--
-- This migration is deliberately one-way. A database migrated here loses its
-- embeddings, and re-indexing on the branch that still has an assistant means
-- recomputing them at roughly two minutes a book. That is the trade the branch
-- exists to make; migrations 001-005 are untouched, as they must be.

-- The FTS index over chunks, and the triggers that fed it. Triggers first:
-- dropping book_chunks while they still reference it leaves them dangling.
DROP TRIGGER IF EXISTS chunks_fts_insert;
DROP TRIGGER IF EXISTS chunks_fts_delete;
DROP TRIGGER IF EXISTS chunks_fts_update;
DROP TABLE IF EXISTS chunks_fts;

-- The vectors, then the chunks they pointed at. chunk_embeddings has a foreign
-- key onto book_chunks, so it goes first.
DROP TABLE IF EXISTS chunk_embeddings;
DROP TABLE IF EXISTS book_embeddings;
DROP INDEX IF EXISTS idx_chunks_book_ordinal;
DROP TABLE IF EXISTS book_chunks;

-- Cached page, chapter and book summaries.
DROP INDEX IF EXISTS idx_summaries_book_kind;
DROP TABLE IF EXISTS summaries;

-- The three stamps that recorded AI work. metadata_fetched_at stays: online
-- metadata lookup is not an AI feature and is still on the roadmap.
ALTER TABLE books DROP COLUMN chunked_at;
ALTER TABLE books DROP COLUMN embedded_at;
ALTER TABLE books DROP COLUMN auto_tagged_at;
