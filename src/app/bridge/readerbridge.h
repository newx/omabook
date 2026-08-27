// ReaderBridge -- the single seam between the foliate-js reader page and
// C++, exposed to QML and, through it, to the page over QWebChannel.
//
// SPEC 5.3: JavaScript owns rendering, pagination, CFI, and visible-range
// text extraction. C++ owns persistence, TTS orchestration and AI. Every
// call across this boundary is one of the small set below.
//
// Nothing here calls into the page. The handshake completes after page
// load, so the page announces itself with readerReady() first -- calling in
// before that evaluates against an undefined function and fails silently
// (CLAUDE.md, "The reader bridge").
#pragma once

#include <QObject>
#include <QString>

class ReaderBridge : public QObject {
    Q_OBJECT
    // Every property below is exposed to QML under the exact identifier the
    // already-ported Reader.qml reads (e.g. `bridgeObject.last_cfi`,
    // `bridgeObject.pdf_page`). cxx-qt never camelCased multi-word
    // qproperty names, so the original Rust build's QML read them as
    // last_cfi/last_fraction/pdf_page, and that QML was carried across
    // unchanged -- so the property tokens here have to match, even though
    // the C++ accessors below stay idiomatic (lastCfi(), pdfPage(), ...).
    Q_PROPERTY(QString last_cfi READ lastCfi NOTIFY lastCfiChanged)
    Q_PROPERTY(double last_fraction READ lastFraction NOTIFY lastFractionChanged)
    Q_PROPERTY(QString chapter READ chapter NOTIFY chapterChanged)
    // Not named `page`: a property named `page` would generate a
    // `pageChanged` notifier, colliding with the `pageChanged` invokable
    // below in a way moc resolves ambiguously. This is why the Rust build
    // named it pdf_page, and the constraint is identical in C++.
    Q_PROPERTY(int pdf_page READ pdfPage NOTIFY pdfPageChanged)
    // Non-empty means the book failed to open.
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    // True once readerReady() has arrived from the page.
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)

public:
    explicit ReaderBridge(QObject *parent = nullptr);

    QString lastCfi() const { return m_lastCfi; }
    double lastFraction() const { return m_lastFraction; }
    QString chapter() const { return m_chapter; }
    int pdfPage() const { return m_pdfPage; }
    QString error() const { return m_error; }
    bool connected() const { return m_connected; }

    // Called by the page on every relocate: stores all five, then emits
    // relocated().
    Q_INVOKABLE void pageChanged(const QString &cfi, double fraction, const QString &text,
                                  const QString &chapter, int page);

    // Called once the page has defined everything C++ may call.
    Q_INVOKABLE void readerReady();

    // Called when the book could not be opened at all.
    Q_INVOKABLE void readerFailed(const QString &message);

    // The text of the page currently on screen, cached from the last
    // pageChanged() call.
    Q_INVOKABLE QString pageText() const { return m_pageText; }

    // The reader highlighted a passage. This persists nothing -- it
    // re-emits highlightRequested() and QML (through NotesModel) owns the
    // save.
    Q_INVOKABLE void saveHighlight(const QString &cfi, const QString &text, double fraction);

    // The reader asked to write a note about a passage. Re-emits
    // noteRequested(); QML owns opening the dialog and nothing is stored
    // until it is accepted.
    Q_INVOKABLE void requestNote(const QString &cfi, const QString &text, double fraction);

signals:
    // Emitted whenever the page moves, so QML can debounce a progress save.
    // Deliberately not named pageChanged: that would collide with the
    // invokable of the same name -- see the pdf_page property comment above,
    // the same constraint applies to this signal's name.
    void relocated();

    void highlightRequested(const QString &cfi, const QString &quote, double fraction);
    void noteRequested(const QString &cfi, const QString &quote, double fraction);

    void lastCfiChanged();
    void lastFractionChanged();
    void chapterChanged();
    void pdfPageChanged();
    void errorChanged();
    void connectedChanged();

private:
    void setLastCfi(const QString &cfi);
    void setLastFraction(double fraction);
    void setChapter(const QString &chapter);
    void setPdfPage(int page);
    void setError(const QString &error);
    void setConnected(bool connected);

    QString m_lastCfi;
    double m_lastFraction = 0.0;
    QString m_chapter;
    int m_pdfPage = 0;
    QString m_error;
    bool m_connected = false;
    // Held for the TTS loop and AI page questions; not persisted.
    QString m_pageText;
};
