#include "readerbridge.h"

ReaderBridge::ReaderBridge(QObject *parent) : QObject(parent) { }

void ReaderBridge::pageChanged(const QString &cfi, double fraction, const QString &text,
                                const QString &chapter, int page) {
    setLastCfi(cfi);
    setLastFraction(qBound(0.0, fraction, 1.0));
    m_pageText = text;
    setChapter(chapter);
    setPdfPage(qMax(0, page));
    emit relocated();
}

void ReaderBridge::readerReady() {
    qInfo("reader page is ready");
    setError(QString());
    setConnected(true);
}

void ReaderBridge::readerFailed(const QString &message) {
    qWarning("reader could not open the book: %s", qUtf8Printable(message));
    setConnected(false);
    setError(message);
}

void ReaderBridge::saveHighlight(const QString &cfi, const QString &text, double fraction) {
    emit highlightRequested(cfi, text, qBound(0.0, fraction, 1.0));
}

void ReaderBridge::requestNote(const QString &cfi, const QString &text, double fraction) {
    emit noteRequested(cfi, text, qBound(0.0, fraction, 1.0));
}

void ReaderBridge::setLastCfi(const QString &cfi) {
    if (m_lastCfi == cfi)
        return;

    m_lastCfi = cfi;
    emit lastCfiChanged();
}

void ReaderBridge::setLastFraction(double fraction) {
    if (m_lastFraction == fraction)
        return;

    m_lastFraction = fraction;
    emit lastFractionChanged();
}

void ReaderBridge::setChapter(const QString &chapter) {
    if (m_chapter == chapter)
        return;

    m_chapter = chapter;
    emit chapterChanged();
}

void ReaderBridge::setPdfPage(int page) {
    if (m_pdfPage == page)
        return;

    m_pdfPage = page;
    emit pdfPageChanged();
}

void ReaderBridge::setError(const QString &error) {
    if (m_error == error)
        return;

    m_error = error;
    emit errorChanged();
}

void ReaderBridge::setConnected(bool connected) {
    if (m_connected == connected)
        return;

    m_connected = connected;
    emit connectedChanged();
}
