#include "noterepository.h"

#include "core/models/book.h" // nowIso8601()

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {

Note buildNote(const QSqlQuery &query) {
    Note note;
    note.id = query.value(0).toLongLong();
    note.bookId = query.value(1).toLongLong();
    note.bookTitle = query.value(2).toString();
    note.cfi = query.value(3).isNull() ? std::nullopt : std::make_optional(query.value(3).toString());
    note.quote = query.value(4).isNull() ? std::nullopt : std::make_optional(query.value(4).toString());
    note.body = query.value(5).isNull() ? std::nullopt : std::make_optional(query.value(5).toString());
    note.pageFraction =
        query.value(6).isNull() ? std::nullopt : std::make_optional(query.value(6).toDouble());
    note.createdAt = query.value(7).toString();
    note.updatedAt = query.value(8).toString();
    return note;
}

const QString kSelectColumns = QStringLiteral(
    "n.id, n.book_id, b.title AS book_title, n.cfi, n.quote, n.body, "
    "n.page_fraction, n.created_at, n.updated_at");

QVariant optionalVariant(const std::optional<QString> &value) {
    return value.has_value() ? QVariant(*value) : QVariant();
}

QVariant optionalVariant(const std::optional<double> &value) {
    return value.has_value() ? QVariant(*value) : QVariant();
}

} // namespace

Result<qint64> NoteRepository::upsert(const NewNote &note) {
    const bool quoteEmpty = !note.quote.has_value() || note.quote->trimmed().isEmpty();
    const bool bodyEmpty = !note.body.has_value() || note.body->trimmed().isEmpty();
    if (quoteEmpty && bodyEmpty)
        return Error::decode(QStringLiteral("a note needs either a quote or a body"));

    const QString now = nowIso8601();
    const bool hasCfi = note.cfi.has_value() && !note.cfi->isEmpty();

    if (hasCfi) {
        qint64 existingId = 0;
        bool found = false;
        {
            QSqlQuery query(m_db);
            if (!query.prepare(
                    QStringLiteral("SELECT id FROM notes WHERE book_id = :bookId AND cfi = :cfi")))
                return Error::db(query.lastError().text());
            query.bindValue(QStringLiteral(":bookId"), note.bookId);
            query.bindValue(QStringLiteral(":cfi"), *note.cfi);
            if (!query.exec())
                return Error::db(query.lastError().text());
            if (query.next()) {
                existingId = query.value(0).toLongLong();
                found = true;
            }
        }

        if (found) {
            // Preserve an existing body when the caller supplies none --
            // re-highlighting must not silently erase what was written.
            QSqlQuery update(m_db);
            if (!update.prepare(QStringLiteral(
                    "UPDATE notes SET "
                    "quote = COALESCE(:quote, quote), "
                    "body = COALESCE(:body, body), "
                    "page_fraction = COALESCE(:pageFraction, page_fraction), "
                    "updated_at = :now "
                    "WHERE id = :id")))
                return Error::db(update.lastError().text());
            update.bindValue(QStringLiteral(":id"), existingId);
            update.bindValue(QStringLiteral(":quote"), optionalVariant(note.quote));
            update.bindValue(QStringLiteral(":body"), optionalVariant(note.body));
            update.bindValue(QStringLiteral(":pageFraction"), optionalVariant(note.pageFraction));
            update.bindValue(QStringLiteral(":now"), now);
            if (!update.exec())
                return Error::db(update.lastError().text());
            return Result<qint64>::ok(existingId);
        }
    }

    QSqlQuery insert(m_db);
    if (!insert.prepare(
            QStringLiteral("INSERT INTO notes "
                            "(book_id, cfi, quote, body, page_fraction, page_label, created_at, updated_at) "
                            "VALUES (:bookId, :cfi, :quote, :body, :pageFraction, :pageLabel, :now, :now)")))
        return Error::db(insert.lastError().text());
    insert.bindValue(QStringLiteral(":bookId"), note.bookId);
    insert.bindValue(QStringLiteral(":cfi"), optionalVariant(note.cfi));
    insert.bindValue(QStringLiteral(":quote"), optionalVariant(note.quote));
    insert.bindValue(QStringLiteral(":body"), optionalVariant(note.body));
    insert.bindValue(QStringLiteral(":pageFraction"), optionalVariant(note.pageFraction));
    insert.bindValue(QStringLiteral(":pageLabel"), optionalVariant(note.pageLabel));
    insert.bindValue(QStringLiteral(":now"), now);
    if (!insert.exec())
        return Error::db(insert.lastError().text());
    return Result<qint64>::ok(insert.lastInsertId().toLongLong());
}

Result<QList<Note>> NoteRepository::forBook(qint64 bookId) const {
    const QString sql = QStringLiteral("SELECT %1 FROM notes n JOIN books b ON b.id = n.book_id "
                                        "WHERE n.book_id = :bookId "
                                        "ORDER BY n.page_fraction ASC NULLS LAST, n.created_at ASC")
                             .arg(kSelectColumns);

    QList<Note> notes;
    {
        QSqlQuery query(m_db);
        if (!query.prepare(sql))
            return Error::db(query.lastError().text());
        query.bindValue(QStringLiteral(":bookId"), bookId);
        if (!query.exec())
            return Error::db(query.lastError().text());
        while (query.next())
            notes.append(buildNote(query));
    }
    return Result<QList<Note>>::ok(notes);
}

Result<QList<Note>> NoteRepository::all() const {
    const QString sql = QStringLiteral("SELECT %1 FROM notes n JOIN books b ON b.id = n.book_id "
                                        "ORDER BY n.created_at DESC")
                             .arg(kSelectColumns);

    QList<Note> notes;
    {
        QSqlQuery query(m_db);
        if (!query.exec(sql))
            return Error::db(query.lastError().text());
        while (query.next())
            notes.append(buildNote(query));
    }
    return Result<QList<Note>>::ok(notes);
}

Result<qint64> NoteRepository::count() const {
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM notes")))
        return Error::db(query.lastError().text());
    if (!query.next())
        return Error::db(QStringLiteral("count query returned no row"));
    return Result<qint64>::ok(query.value(0).toLongLong());
}

Result<void> NoteRepository::deleteNote(qint64 id) {
    QSqlQuery query(m_db);
    if (!query.prepare(QStringLiteral("DELETE FROM notes WHERE id = :id")))
        return Error::db(query.lastError().text());
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec())
        return Error::db(query.lastError().text());
    return VoidResult::ok();
}

Result<void> NoteRepository::setBody(qint64 id, const QString &body) {
    QSqlQuery query(m_db);
    if (!query.prepare(QStringLiteral("UPDATE notes SET body = :body, updated_at = :now WHERE id = :id")))
        return Error::db(query.lastError().text());
    query.bindValue(QStringLiteral(":id"), id);
    query.bindValue(QStringLiteral(":body"), body);
    query.bindValue(QStringLiteral(":now"), nowIso8601());
    if (!query.exec())
        return Error::db(query.lastError().text());
    return VoidResult::ok();
}
