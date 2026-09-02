// Highlights and notes.
//
// One table holds both. A highlight is a note with a quote and no body;
// keeping them apart would mean duplicating the anchoring, the ordering, and
// the reader's draw path for no gain.
#pragma once

#include "core/result.h"

#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <optional>

// A highlight or a note, anchored into a book.
struct Note {
    qint64 id = 0;
    qint64 bookId = 0;
    QString bookTitle;
    // Foliate's canonical fragment identifier -- where this sits in the book.
    std::optional<QString> cfi;
    // The selected text.
    std::optional<QString> quote;
    // The reader's own words. Absent for a plain highlight.
    std::optional<QString> body;
    std::optional<double> pageFraction;
    QString createdAt;
    QString updatedAt;

    // A highlight is a quote with nothing written about it.
    bool isHighlightOnly() const {
        const bool hasQuoteText = quote.has_value() && !quote->trimmed().isEmpty();
        return !hasBody() && hasQuoteText;
    }

    bool hasBody() const { return body.has_value() && !body->trimmed().isEmpty(); }
};

// What to store. Separate from Note so creating one cannot invent an id.
struct NewNote {
    qint64 bookId = 0;
    std::optional<QString> cfi;
    std::optional<QString> quote;
    std::optional<QString> body;
    std::optional<double> pageFraction;
    std::optional<QString> pageLabel;
};

// See BookRepository for why this takes a QSqlDatabase & rather than owning
// one.
class NoteRepository {
public:
    explicit NoteRepository(QSqlDatabase &db) : m_db(db) { }

    // Create, or update the annotation already anchored at the same place.
    // Highlighting a passage twice must not produce two rows: matches an
    // existing row on (book_id, cfi) when cfi is non-empty, and
    // COALESCE-preserves any field left unset so re-highlighting never
    // erases a body already written. Rejects an annotation with neither
    // quote nor body.
    Result<qint64> upsert(const NewNote &note);

    // Every annotation in one book, in reading order: page_fraction
    // ascending with NULLs last, then created_at.
    Result<QList<Note>> forBook(qint64 bookId) const;

    // Everything across the library, newest first -- the Highlights & notes
    // view.
    Result<QList<Note>> all() const;

    Result<qint64> count() const;

    // Named deleteNote, not delete, because the latter is a C++ keyword.
    Result<void> deleteNote(qint64 id);

    Result<void> setBody(qint64 id, const QString &body);

private:
    QSqlDatabase &m_db;
};
