// NotesModel -- highlights and notes, both in the reader and as a
// library-wide list (SPEC 5.3).
//
// A highlight is a note with a quote and no body, which is why one model and
// one table serve both.
#pragma once

#include "core/repo/noterepository.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

class NotesModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // 0 lists every book's annotations; otherwise just this book's.
    //
    // Exposed to QML as book_id, not the usual camelCase bookId: the QML
    // (Main.qml: `notesModel.book_id = 0`) reads that identifier, and the QML
    // is the contract. The C++ accessors below stay idiomatic; only the
    // property token QML sees has to match.
    Q_PROPERTY(qint64 book_id READ bookId WRITE setBookId NOTIFY bookIdChanged)

public:
    enum Role {
        NoteIdRole = Qt::UserRole,
        BookIdRole,
        BookTitleRole,
        QuoteRole,
        BodyRole,
        CfiRole,
        IsHighlightRole,
        CreatedAtRole,
    };

    explicit NotesModel(QObject *parent = nullptr);

    int count() const { return m_count; }
    qint64 bookId() const { return m_bookId; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Re-query everything this model currently scopes to (all books, or
    // just book_id) and reset. A full re-query is a model reset, which is
    // correct and simplest here (CLAUDE.md, "QML conventions").
    Q_INVOKABLE void reload();

    // Create, or update the annotation already anchored at (bookId, cfi).
    // Returns the row id, or 0 on failure.
    Q_INVOKABLE qint64 saveAnnotation(qint64 bookId, const QString &cfi, const QString &quote,
                                       const QString &body, double fraction);
    Q_INVOKABLE void setBody(qint64 id, const QString &body);
    Q_INVOKABLE void remove(qint64 id);

    // Every annotation of one book as `[{"id":N,"cfi":"...","hasBody":bool}]`,
    // for the reader page to paint. A note with no CFI is skipped -- there is
    // nothing on the page to anchor it to.
    Q_INVOKABLE QString annotationsJson(qint64 bookId) const;

    // One annotation by id, as {"bookId": n, "cfi": "..."}; an empty map when
    // there is no such note. The Highlights view already has the row in hand,
    // so this exists for callers that have only an id.
    Q_INVOKABLE QVariantMap noteById(qint64 id) const;

    // The date portion of an RFC3339 timestamp -- what CreatedAtRole shows.
    // Static so it is testable without a model or a database (CLAUDE.md,
    // "Pure logic goes in static member functions").
    static QString datePortion(const QString &timestamp);

signals:
    void countChanged();
    void bookIdChanged();

private:
    void setCount(int count);
    void setBookId(qint64 bookId);

    QList<Note> m_notes;
    int m_count = 0;
    qint64 m_bookId = 0;
};
