// Deciding a book's category without asking anyone, ported from
// omabook-core/src/import/classify.rs.
//
// Categories used to come only from the folder a book was imported from,
// which works for people who keep a sorted library and gives everyone else
// nothing. Publisher subjects carry the same information in a more
// structured form than they look:
//
//   BISAC, in commercial EPUBs:  "FICTION / Science Fiction / General"
//   Library of Congress style:   "Astronomy -- Early works to 1800"
//
// The head of either is a category. A small table folds the many heads
// that mean the same shelf into one name, so "COMPUTERS", "Programming"
// and "Software engineering" do not become three sidebar rows. Anything
// the table does not know passes through as itself, title-cased, rather
// than being dropped: a wrong-but-honest category beats none.
//
// No network, no model -- a library should be browsable the moment it is
// imported.
#pragma once

#include "core/import/metadata.h"

#include <QString>
#include <optional>

// The category for a book, best source first:
// 1. the folder it was imported from, when it had one -- a sorted library
//    is a stronger signal than any publisher's;
// 2. a known shelf named anywhere in its subjects, else the head of the
//    first subject that can stand as a category by itself;
// 3. a known shelf mentioned in its title or description;
// 4. nothing -- a book with no signal stays uncategorised rather than
//    guessed.
std::optional<QString> categoryFor(const QString &folder, const FileMetadata &meta);

// The part of a subject before the first BISAC "/" or LoC "--" separator
// (earliest match among the four spellings wins), trimmed and with a
// trailing comma stripped. Exposed so tests can exercise it directly
// (CLAUDE.md, "Pure logic goes in static member functions").
QString head(const QString &subject);

// Two or three whitespace-separated words that each start with an
// uppercase letter and are not themselves fully uppercase, as in "Paula
// Appling" or "Andrew D. Hwang". PDF keyword fields are full of these --
// the people who proofread a Gutenberg scan -- and a name is never a
// shelf. Library subjects are sentence case ("Mathematical recreations"),
// so they pass; "MOBY DICK" does not, because it is fully shouted.
bool looksLikeAName(const QString &text);
