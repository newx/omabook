-- Favourites, for the sidebar's Favorites view.
--
-- A column rather than a join table: this is a single-user app, so a favourite
-- is a property of the book, not a relationship between a book and a user.
ALTER TABLE books ADD COLUMN is_favorite INTEGER NOT NULL DEFAULT 0;
CREATE INDEX idx_books_favorite ON books(is_favorite) WHERE is_favorite = 1;
