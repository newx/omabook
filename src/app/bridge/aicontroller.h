// AiController -- summaries, questions, and indexing, exposed to QML
// (SPEC 5.6-5.8). Ported from omabook/crates/omabook-app/src/bridge/ai.rs.
//
// Everything long-running happens off the GUI thread and reports back
// through signals, so the window never blocks. Indexing additionally
// honours WorkPolicy: it is opt-in, local-only, and pauses off mains power
// (SPEC 5.5). This object is instantiated once per screen that needs it
// (the reader's panel, and the library's Ask page each get their own, SPEC
// 5.13), so it holds no static or shared mutable state.
#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QPair>
#include <QString>

#include "core/ai/assistant.h"

#include <functional>
#include <optional>

class QThread;
class IndexWorker;

class AiController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString answer READ answer NOTIFY answerChanged)
    // \n\n-joined excerpts of whatever passages the last answer used.
    Q_PROPERTY(QString sources READ sources NOTIFY sourcesChanged)
    Q_PROPERTY(QString provider READ provider NOTIFY providerChanged)
    // Comma list of "local"/"remote" that can actually be reached, or "none".
    Q_PROPERTY(QString available_providers READ availableProviders NOTIFY availableProvidersChanged)
    Q_PROPERTY(bool indexing READ indexing NOTIFY indexingChanged)
    Q_PROPERTY(int index_done READ indexDone NOTIFY indexDoneChanged)
    Q_PROPERTY(int index_total READ indexTotal NOTIFY indexTotalChanged)
    Q_PROPERTY(bool background_enabled READ backgroundEnabled NOTIFY backgroundEnabledChanged)
    Q_PROPERTY(bool background_on_battery READ backgroundOnBattery NOTIFY backgroundOnBatteryChanged)
    Q_PROPERTY(bool on_mains READ onMains NOTIFY onMainsChanged)
    // "" while idle, otherwise "ollama" or "kokoro".
    Q_PROPERTY(QString starting READ starting NOTIFY startingChanged)
    Q_PROPERTY(QString service_message READ serviceMessage NOTIFY serviceMessageChanged)
    Q_PROPERTY(QString local_model READ localModel NOTIFY localModelChanged)
    // Every model Ollama has pulled, newline-separated for QML.
    Q_PROPERTY(QString local_models READ localModels NOTIFY localModelsChanged)
    Q_PROPERTY(QString remote_model READ remoteModel NOTIFY remoteModelChanged)
    // The remote provider's key itself is never exposed to QML -- only
    // whether one is configured.
    Q_PROPERTY(bool has_remote_key READ hasRemoteKey NOTIFY hasRemoteKeyChanged)

public:
    explicit AiController(QObject *parent = nullptr);
    ~AiController() override;

    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QString answer() const { return m_answer; }
    QString sources() const { return m_sources; }
    QString provider() const { return m_provider; }
    QString availableProviders() const { return m_availableProviders; }
    bool indexing() const { return m_indexing; }
    int indexDone() const { return m_indexDone; }
    int indexTotal() const { return m_indexTotal; }
    bool backgroundEnabled() const { return m_backgroundEnabled; }
    bool backgroundOnBattery() const { return m_backgroundOnBattery; }
    bool onMains() const { return m_onMains; }
    QString starting() const { return m_starting; }
    QString serviceMessage() const { return m_serviceMessage; }
    QString localModel() const { return m_localModel; }
    QString localModels() const { return m_localModels; }
    QString remoteModel() const { return m_remoteModel; }
    bool hasRemoteKey() const { return m_hasRemoteKey; }

    // Summarize the page currently on screen.
    Q_INVOKABLE void summarizePage(const QString &text, const QString &book, const QString &chapter);

    // Ask about the open book. `scope` is "page", "so_far" or "book".
    Q_INVOKABLE void askBook(qint64 bookId, const QString &question, const QString &scope,
                              const QString &pageText, qint64 ordinal);

    // Ask about the library. Emits libraryAnswered with the matching book
    // ids, which the grid then shows in rank order -- even when there is no
    // prose answer, because the books are the product and the prose is a
    // bonus (SPEC 5.8).
    Q_INVOKABLE void askLibrary(const QString &question);

    // Index every book that has none, one at a time. Honours the work
    // policy, re-checked per book: local only, and it stops when the
    // policy says so.
    Q_INVOKABLE void indexLibrary();

    // Prepare one book for questions: chunk it, then embed it.
    Q_INVOKABLE void indexBook(qint64 bookId);

    // Stop an index run. Work already done is kept.
    Q_INVOKABLE void cancelIndexing();

    // How far a book is prepared: "none", "partial" or "ready".
    Q_INVOKABLE QString indexState(qint64 bookId) const;

    Q_INVOKABLE void setBackgroundEnabled(bool enabled);
    Q_INVOKABLE void setBackgroundOnBattery(bool enabled);

    // Start the local Ollama server.
    Q_INVOKABLE void startOllama();
    // Start the Kokoro speech container.
    Q_INVOKABLE void startKokoro();

    // Re-probe providers and power. Synchronous and cheap enough to run on
    // the GUI thread -- unlike summarize/ask/index/start/refreshModels,
    // this is not in the set of calls the task brief requires off-thread.
    Q_INVOKABLE void refresh();

    Q_INVOKABLE void clearAnswer();

    // Ask Ollama which models it has pulled. Off the UI thread.
    Q_INVOKABLE void refreshModels();

    Q_INVOKABLE void setLocalModel(const QString &model);
    Q_INVOKABLE void setRemoteModel(const QString &model);
    // Store the key for the remote provider. Empty clears it.
    Q_INVOKABLE void setRemoteKey(const QString &key);

signals:
    // Comma-separated book ids, best match first.
    void libraryAnswered(const QString &ids);

    void busyChanged();
    void statusChanged();
    void answerChanged();
    void sourcesChanged();
    void providerChanged();
    void availableProvidersChanged();
    void indexingChanged();
    void indexDoneChanged();
    void indexTotalChanged();
    void backgroundEnabledChanged();
    void backgroundOnBatteryChanged();
    void onMainsChanged();
    void startingChanged();
    void serviceMessageChanged();
    void localModelChanged();
    void localModelsChanged();
    void remoteModelChanged();
    void hasRemoteKeyChanged();

private slots:
    // Shared by summarizePage and askBook: both produce a Result<Answer>
    // and report it identically.
    void onAnswerReady();
    void onLibraryAnswerReady();
    void onModelsReady();
    void onServiceStarted();

    void onIndexStatus(const QString &status);
    void onIndexProgress(int done, int total);
    void onIndexFinished(const QString &message);

private:
    void setBusy(bool busy);
    void setStatus(const QString &status);
    void setAnswer(const QString &answer);
    void setSources(const QString &sources);
    void setProvider(const QString &provider);
    void setAvailableProviders(const QString &providers);
    void setIndexing(bool indexing);
    void setIndexDone(int done);
    void setIndexTotal(int total);
    void setOnMains(bool onMains);
    void setStarting(const QString &starting);
    void setServiceMessage(const QString &message);
    void setLocalModels(const QString &models);
    void setHasRemoteKey(bool present);

    // Starts a fresh worker thread for one index run, wiring its signals to
    // this controller. `kickoff` is called once the thread's event loop is
    // running, on the worker thread, to invoke the right job with its
    // arguments (CLAUDE.md, "Threading", pattern 1).
    void startIndexRun(const std::function<void(IndexWorker *)> &kickoff);

    bool m_busy = false;
    QString m_status;
    QString m_answer;
    QString m_sources;
    QString m_provider;
    QString m_availableProviders = QStringLiteral("none");
    bool m_indexing = false;
    int m_indexDone = 0;
    int m_indexTotal = 0;
    bool m_backgroundEnabled = false;
    bool m_backgroundOnBattery = false;
    bool m_onMains = true;
    QString m_starting;
    QString m_serviceMessage;
    QString m_localModel;
    QString m_localModels;
    QString m_remoteModel;
    bool m_hasRemoteKey = false;

    // Reused across calls of the same shape; harmless because busy guards
    // summarizePage/askBook/askLibrary against overlapping, and starting
    // guards startOllama/startKokoro the same way.
    QFutureWatcher<Result<Answer>> *m_answerWatcher = nullptr;
    QFutureWatcher<Result<QPair<QList<qint64>, std::optional<Answer>>>> *m_libraryWatcher = nullptr;
    QFutureWatcher<QString> *m_modelsWatcher = nullptr;
    QFutureWatcher<QString> *m_serviceWatcher = nullptr;

    // A fresh worker/thread pair per index run (CLAUDE.md, "Threading",
    // pattern 1) rather than one kept alive for this controller's whole
    // life -- mirrors the Rust original spawning a fresh thread per call.
    // Both are null whenever no run is in flight; written and read only on
    // the GUI thread (set here, cleared in onIndexFinished()), so no
    // synchronization is needed for the pointers themselves. cancelIndexing()
    // still reaches across into the worker thread, but only to flip its
    // std::atomic_bool, which is safe to write from any thread.
    QThread *m_indexThread = nullptr;
    IndexWorker *m_indexWorker = nullptr;
};
