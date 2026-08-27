#include "indexworker.h"

#include "core/ai/indexer.h"
#include "core/ai/ollama.h"
#include "core/ai/policy.h"
#include "core/ai/power.h"
#include "core/ai/vectors.h"
#include "core/db/database.h"
#include "core/repo/bookrepository.h"

IndexWorker::IndexWorker(QObject *parent) : QObject(parent) { }

void IndexWorker::cancel() {
    m_keepGoing.store(false, std::memory_order_relaxed);
}

void IndexWorker::runLibrary(bool backgroundEnabled, bool backgroundOnBattery) {
    // This worker is reused across runs (one thread for the controller's
    // whole life), so a previous run's cancellation must not silently
    // no-op this one.
    m_keepGoing.store(true, std::memory_order_relaxed);

    WorkPolicy policy;
    policy.backgroundEnabled = backgroundEnabled;
    policy.backgroundOnBattery = backgroundOnBattery;

    QSqlDatabase &db = Database::forCurrentThread().connection();
    const BookRepository repo(db);
    const Result<QList<Book>> listed = repo.list(LibraryFilter::all(), BookSort::Title);
    if (listed.isErr()) {
        emit finished(QStringLiteral("could not list books: %1").arg(listed.error().message));
        return;
    }

    // Built once per run, not per book: Ollama owns a QNetworkAccessManager
    // that must live on the thread that uses it (CLAUDE.md, "Threading"),
    // and this whole function runs on this worker's own thread.
    Ollama embedder = Ollama::fromEnv();

    int done = 0;
    int skipped = 0;

    for (const Book &book : listed.value()) {
        // Checked before every book, not just at the start, so unplugging
        // the laptop mid-run is noticed promptly.
        if (!m_keepGoing.load(std::memory_order_relaxed)) {
            emit finished(QStringLiteral("stopped after %1 book(s); progress is kept").arg(done));
            return;
        }

        const Decision decision =
                policy.permits(TaskKind::Embed, Trigger::Background, ProviderClass::Local, currentPower());
        if (!decision.isAllowed()) {
            emit finished(QStringLiteral("%1 — indexed %2 book(s)").arg(decision.reason()).arg(done));
            return;
        }

        const VectorStore store(db);
        const Result<QPair<qint64, qint64>> coverage = store.chunkCoverage(book.id);
        if (coverage.isOk() && coverage.value().second > 0 && coverage.value().first >= coverage.value().second) {
            ++skipped;
            continue;
        }

        emit statusChanged(QStringLiteral("indexing %1").arg(book.title));

        const Result<int> chunked = Indexer::chunkBook(db, book.id);
        if (chunked.isErr()) {
            qWarning("chunking failed for book %s: %s", qUtf8Printable(book.title),
                     qUtf8Printable(chunked.error().message));
            continue;
        }

        const Result<Indexer::IndexReport> embedded =
                Indexer::embedBook(db, book.id, embedder, [](int, int) { },
                                    [this]() { return m_keepGoing.load(std::memory_order_relaxed); });
        if (embedded.isErr()) {
            qWarning("embedding failed for book %s: %s", qUtf8Printable(book.title),
                     qUtf8Printable(embedded.error().message));
            continue;
        }

        // A metadata-embedding failure does not fail the book: the book's
        // chunks are already searchable, and library-wide search is a
        // bonus on top of that (matches the Rust original's `let _ =`).
        Indexer::embedBookMetadata(db, book.id, embedder);
        ++done;
    }

    emit finished(QStringLiteral("indexed %1 book(s), %2 already done").arg(done).arg(skipped));
}

void IndexWorker::runBook(qint64 bookId) {
    m_keepGoing.store(true, std::memory_order_relaxed);

    QSqlDatabase &db = Database::forCurrentThread().connection();
    Ollama embedder = Ollama::fromEnv();

    const Result<int> chunked = Indexer::chunkBook(db, bookId);
    if (chunked.isErr()) {
        emit finished(QStringLiteral("indexing failed: %1").arg(chunked.error().message));
        return;
    }

    const Result<Indexer::IndexReport> embedded = Indexer::embedBook(
            db, bookId, embedder,
            [this](int done, int total) { emit progressChanged(done, total); },
            [this]() { return m_keepGoing.load(std::memory_order_relaxed); });
    if (embedded.isErr()) {
        emit finished(QStringLiteral("indexing failed: %1").arg(embedded.error().message));
        return;
    }

    const Result<bool> metaEmbedded = Indexer::embedBookMetadata(db, bookId, embedder);
    if (metaEmbedded.isErr()) {
        emit finished(QStringLiteral("indexing failed: %1").arg(metaEmbedded.error().message));
        return;
    }

    emit finished(QStringLiteral("ready for questions"));
}
