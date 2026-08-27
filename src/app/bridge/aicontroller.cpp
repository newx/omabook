#include "aicontroller.h"
#include "indexworker.h"

#include "core/ai/anthropic.h"
#include "core/ai/ollama.h"
#include "core/ai/policy.h"
#include "core/ai/power.h"
#include "core/ai/provider.h"
#include "core/ai/vectors.h"
#include "core/db/database.h"
#include "core/repo/bookrepository.h"
#include "core/repo/settingsrepository.h"
#include "core/services.h"
#include "core/tts/kokoro.h"

#include "app/assets.h"

#include <QThread>
#include <QtConcurrent>

namespace {

const QString SETTING_BACKGROUND = QStringLiteral("ai_background_enabled");
const QString SETTING_ON_BATTERY = QStringLiteral("ai_background_on_battery");
const QString SETTING_LOCAL_MODEL = QStringLiteral("ai_local_model");
const QString SETTING_REMOTE_MODEL = QStringLiteral("ai_remote_model");
// The remote provider's key. Stored in the library database, which is
// owner-only on disk -- see Database::open.
const QString SETTING_REMOTE_KEY = QStringLiteral("ai_remote_key");

// core/ai/ollama.h and anthropic.h each resolve their own env-or-default
// model name internally (Ollama::fromEnv(), Anthropic::fromEnv()), but
// neither exposes an accessor for the result -- only
// Ollama::fromEnv().baseUrl() is public; the model literals are file-private
// to ollama.cpp/anthropic.cpp. These mirror the same literals and
// environment variable names those two files already use, so a setting's
// absence falls back to exactly what the provider itself would have chosen.
const QString DEFAULT_LOCAL_CHAT_MODEL = QStringLiteral("llama3.2:3b");
const QString DEFAULT_LOCAL_EMBED_MODEL = QStringLiteral("nomic-embed-text");
const QString DEFAULT_REMOTE_MODEL = QStringLiteral("claude-opus-5");

using LibraryAnswer = QPair<QList<qint64>, std::optional<Answer>>;

QString envOr(const char *name, const QString &fallback) {
    if (!qEnvironmentVariableIsSet(name))
        return fallback;
    return QString::fromUtf8(qgetenv(name));
}

// A stored setting, treating blank as absent.
std::optional<QString> stored(QSqlDatabase &db, const QString &key) {
    const SettingsRepository settings(db);
    const Result<std::optional<QString>> found = settings.get(key);
    if (found.isErr() || !found.value())
        return std::nullopt;
    if (found.value()->trimmed().isEmpty())
        return std::nullopt;
    return found.value();
}

// Settings first, then the environment, then the built-in default -- that
// order and not the reverse: a choice made in Settings is the more
// deliberate of the two, and it would be baffling for an exported variable
// to quietly override the picker that claims to control it.
QString settingOrEnv(QSqlDatabase &db, const QString &key, const char *envVar, const QString &fallback) {
    const std::optional<QString> byKey = stored(db, key);
    if (byKey)
        return *byKey;
    return envOr(envVar, fallback);
}

QString localModelName(QSqlDatabase &db) {
    return settingOrEnv(db, SETTING_LOCAL_MODEL, "OLLAMA_CHAT_MODEL", DEFAULT_LOCAL_CHAT_MODEL);
}

QString remoteModelName(QSqlDatabase &db) {
    return settingOrEnv(db, SETTING_REMOTE_MODEL, "OMABOOK_REMOTE_MODEL", DEFAULT_REMOTE_MODEL);
}

std::optional<QString> remoteKey(QSqlDatabase &db) {
    const std::optional<QString> byKey = stored(db, SETTING_REMOTE_KEY);
    if (byKey)
        return byKey;
    if (!qEnvironmentVariableIsSet("ANTHROPIC_API_KEY"))
        return std::nullopt;
    const QString key = QString::fromUtf8(qgetenv("ANTHROPIC_API_KEY"));
    if (key.trimmed().isEmpty())
        return std::nullopt;
    return key;
}

// The local provider, configured from settings. Built fresh per call rather
// than kept as a member: it owns a QNetworkAccessManager that must live on
// the thread that uses it (CLAUDE.md, "Threading"), and every caller here
// constructs one inside the thread that will actually make the request.
Ollama localProvider(QSqlDatabase &db) {
    return Ollama(Ollama::fromEnv().baseUrl(), localModelName(db),
                  envOr("OLLAMA_EMBED_MODEL", DEFAULT_LOCAL_EMBED_MODEL));
}

// The remote provider, or nullopt when no key is configured -- which is the
// normal case, since it is opt-in.
std::optional<Anthropic> remoteProviderFor(QSqlDatabase &db) {
    const std::optional<QString> key = remoteKey(db);
    if (!key)
        return std::nullopt;
    return std::optional<Anthropic>(std::in_place, *key, remoteModelName(db));
}

// Which providers can be reached, as a comma-separated list for QML.
QString detectProviders(QSqlDatabase &db) {
    QStringList found;

    Ollama local = localProvider(db);
    if (local.available())
        found << QStringLiteral("local");

    std::optional<Anthropic> remote = remoteProviderFor(db);
    if (remote && remote->available())
        found << QStringLiteral("remote");

    if (found.isEmpty())
        return QStringLiteral("none");
    return found.join(QLatin1Char(','));
}

WorkPolicy policyFrom(bool backgroundEnabled, bool backgroundOnBattery) {
    WorkPolicy policy;
    policy.backgroundEnabled = backgroundEnabled;
    policy.backgroundOnBattery = backgroundOnBattery;
    return policy;
}

// Build the providers and ask the assistant a question. Constructed per
// call because each provider is cheap and this keeps no sockets open
// between questions (matches the Rust original's with_assistant).
Result<Answer> summarizeTask(QString text, QString book, QString chapter, WorkPolicy policy) {
    QSqlDatabase &db = Database::forCurrentThread().connection();
    Ollama local = localProvider(db);
    std::optional<Anthropic> remote = remoteProviderFor(db);
    Assistant assistant(db, local, remote ? &*remote : nullptr, local, policy);
    return assistant.summarizePage(text, book, chapter);
}

Result<Answer> askBookTask(qint64 bookId, QString question, QString scopeText, QString pageText, qint64 ordinal,
                            WorkPolicy policy) {
    QSqlDatabase &db = Database::forCurrentThread().connection();
    Ollama local = localProvider(db);
    std::optional<Anthropic> remote = remoteProviderFor(db);
    Assistant assistant(db, local, remote ? &*remote : nullptr, local, policy);

    const Scope scope = Assistant::parseScope(scopeText);
    const std::optional<qint64> limit = ordinal >= 0 ? std::optional<qint64>(ordinal) : std::nullopt;
    return assistant.askBook(bookId, question, scope, pageText, limit);
}

Result<LibraryAnswer> askLibraryTask(QString question, WorkPolicy policy) {
    QSqlDatabase &db = Database::forCurrentThread().connection();
    Ollama local = localProvider(db);
    std::optional<Anthropic> remote = remoteProviderFor(db);
    Assistant assistant(db, local, remote ? &*remote : nullptr, local, policy);
    return assistant.askLibrary(question);
}

} // namespace

AiController::AiController(QObject *parent) : QObject(parent) {
    QSqlDatabase &db = Database::forCurrentThread().connection();

    const SettingsRepository settings(db);
    m_backgroundEnabled = settings.getOr(SETTING_BACKGROUND, QStringLiteral("false")) == QStringLiteral("true");
    m_backgroundOnBattery =
            settings.getOr(SETTING_ON_BATTERY, QStringLiteral("false")) == QStringLiteral("true");
    m_onMains = currentPower() == Power::Mains;
    m_localModel = localModelName(db);
    m_remoteModel = remoteModelName(db);
    m_hasRemoteKey = remoteKey(db).has_value();
    m_availableProviders = detectProviders(db);

    m_answerWatcher = new QFutureWatcher<Result<Answer>>(this);
    connect(m_answerWatcher, &QFutureWatcherBase::finished, this, &AiController::onAnswerReady);

    m_libraryWatcher = new QFutureWatcher<Result<LibraryAnswer>>(this);
    connect(m_libraryWatcher, &QFutureWatcherBase::finished, this, &AiController::onLibraryAnswerReady);

    m_modelsWatcher = new QFutureWatcher<QString>(this);
    connect(m_modelsWatcher, &QFutureWatcherBase::finished, this, &AiController::onModelsReady);

    m_serviceWatcher = new QFutureWatcher<QString>(this);
    connect(m_serviceWatcher, &QFutureWatcherBase::finished, this, &AiController::onServiceStarted);
}

AiController::~AiController() {
    // An index run may still be in flight when a screen closes. Ask it to
    // stop and wait for it, rather than leaving a thread running past this
    // object's lifetime (CLAUDE.md, "Threading"). Both pointers are null
    // once a run has already finished on its own.
    if (m_indexThread) {
        if (m_indexWorker)
            m_indexWorker->cancel();
        m_indexThread->quit();
        m_indexThread->wait();
    }
}

void AiController::summarizePage(const QString &text, const QString &book, const QString &chapter) {
    if (m_busy)
        return;

    setBusy(true);
    setStatus(QStringLiteral("summarizing…"));
    setAnswer(QString());

    const WorkPolicy policy = policyFrom(m_backgroundEnabled, m_backgroundOnBattery);
    m_answerWatcher->setFuture(QtConcurrent::run(summarizeTask, text, book, chapter, policy));
}

void AiController::askBook(qint64 bookId, const QString &question, const QString &scope, const QString &pageText,
                            qint64 ordinal) {
    if (m_busy)
        return;

    setBusy(true);
    setStatus(QStringLiteral("thinking…"));
    setAnswer(QString());

    const WorkPolicy policy = policyFrom(m_backgroundEnabled, m_backgroundOnBattery);
    m_answerWatcher->setFuture(QtConcurrent::run(askBookTask, bookId, question, scope, pageText, ordinal, policy));
}

void AiController::onAnswerReady() {
    const Result<Answer> outcome = m_answerWatcher->result();

    // Results first, busy last: setting a property emits its notifier
    // synchronously, so a Connections.onBusyChanged handler would otherwise
    // wake to find the previous answer still in place (task brief).
    if (outcome.isOk()) {
        setAnswer(outcome.value().text);
        setProvider(outcome.value().provider);
        setSources(outcome.value().sources.join(QStringLiteral("\n\n")));
        setStatus(QString());
    } else {
        setStatus(outcome.error().message);
    }
    setBusy(false);
}

void AiController::askLibrary(const QString &question) {
    if (m_busy)
        return;

    setBusy(true);
    setStatus(QStringLiteral("searching your library…"));
    setAnswer(QString());
    setSources(QString());

    const WorkPolicy policy = policyFrom(m_backgroundEnabled, m_backgroundOnBattery);
    m_libraryWatcher->setFuture(QtConcurrent::run(askLibraryTask, question, policy));
}

void AiController::onLibraryAnswerReady() {
    const Result<LibraryAnswer> outcome = m_libraryWatcher->result();

    if (outcome.isErr()) {
        setStatus(outcome.error().message);
        setBusy(false);
        return;
    }

    const QList<qint64> ids = outcome.value().first;
    const std::optional<Answer> &prose = outcome.value().second;

    QStringList idStrings;
    for (qint64 id : ids)
        idStrings << QString::number(id);

    if (prose) {
        setAnswer(prose->text);
        setProvider(prose->provider);
    }
    setStatus(ids.isEmpty() ? QStringLiteral("nothing in your library matched — index some books first")
                             : QString());

    // Emitted even with no prose answer: the books are the product, the
    // prose is a bonus (SPEC 5.8). Before busy is cleared, for the same
    // ordering reason as onAnswerReady().
    emit libraryAnswered(idStrings.join(QLatin1Char(',')));
    setBusy(false);
}

void AiController::indexLibrary() {
    if (m_indexing)
        return;

    setIndexing(true);
    setStatus(QStringLiteral("indexing library…"));

    const bool backgroundEnabled = m_backgroundEnabled;
    const bool backgroundOnBattery = m_backgroundOnBattery;
    startIndexRun([backgroundEnabled, backgroundOnBattery](IndexWorker *worker) {
        worker->runLibrary(backgroundEnabled, backgroundOnBattery);
    });
}

void AiController::indexBook(qint64 bookId) {
    if (m_indexing)
        return;

    setIndexing(true);
    setIndexDone(0);
    setIndexTotal(0);
    setStatus(QStringLiteral("preparing…"));

    startIndexRun([bookId](IndexWorker *worker) { worker->runBook(bookId); });
}

void AiController::cancelIndexing() {
    if (m_indexWorker)
        m_indexWorker->cancel();
    setStatus(QStringLiteral("stopping…"));
}

QString AiController::indexState(qint64 bookId) const {
    QSqlDatabase &db = Database::forCurrentThread().connection();
    const VectorStore store(db);
    const Result<QPair<qint64, qint64>> coverage = store.chunkCoverage(bookId);
    if (coverage.isErr())
        return QStringLiteral("none");

    const qint64 done = coverage.value().first;
    const qint64 total = coverage.value().second;
    if (done == 0)
        return QStringLiteral("none");
    if (total > 0 && done >= total)
        return QStringLiteral("ready");
    return QStringLiteral("partial");
}

void AiController::onIndexStatus(const QString &status) {
    setStatus(status);
}

void AiController::onIndexProgress(int done, int total) {
    setIndexDone(done);
    setIndexTotal(total);
}

void AiController::onIndexFinished(const QString &message) {
    setStatus(message);
    setIndexing(false);
    // Both pointers refer to an object that has just asked to be deleted
    // (deleteLater, still pending); nulling them here rather than waiting
    // for the actual deletion is what keeps cancelIndexing() from ever
    // reaching into a worker whose job is already done.
    m_indexWorker = nullptr;
    m_indexThread = nullptr;
}

void AiController::setBackgroundEnabled(bool enabled) {
    if (m_backgroundEnabled == enabled)
        return;

    m_backgroundEnabled = enabled;
    emit backgroundEnabledChanged();

    QSqlDatabase &db = Database::forCurrentThread().connection();
    SettingsRepository settings(db);
    const Result<void> persisted =
            settings.set(SETTING_BACKGROUND, enabled ? QStringLiteral("true") : QStringLiteral("false"));
    if (persisted.isErr())
        qWarning("could not persist AI setting: %s", qUtf8Printable(persisted.error().message));
}

void AiController::setBackgroundOnBattery(bool enabled) {
    if (m_backgroundOnBattery == enabled)
        return;

    m_backgroundOnBattery = enabled;
    emit backgroundOnBatteryChanged();

    QSqlDatabase &db = Database::forCurrentThread().connection();
    SettingsRepository settings(db);
    const Result<void> persisted =
            settings.set(SETTING_ON_BATTERY, enabled ? QStringLiteral("true") : QStringLiteral("false"));
    if (persisted.isErr())
        qWarning("could not persist AI setting: %s", qUtf8Printable(persisted.error().message));
}

void AiController::startOllama() {
    if (!m_starting.isEmpty())
        return;

    setStarting(QStringLiteral("ollama"));
    setServiceMessage(QStringLiteral("Starting Ollama…"));

    m_serviceWatcher->setFuture(QtConcurrent::run([]() {
        const Result<void> outcome = Services::startOllama([]() { return Ollama::fromEnv().available(); });
        return outcome.isOk() ? QStringLiteral("Ollama is running.") : outcome.error().message;
    }));
}

void AiController::startKokoro() {
    if (!m_starting.isEmpty())
        return;

    setStarting(QStringLiteral("kokoro"));
    setServiceMessage(
            QStringLiteral("Starting Kokoro. The image is large, so this can take a minute…"));

    m_serviceWatcher->setFuture(QtConcurrent::run([]() {
        const Result<void> outcome =
                Services::startKokoro(Assets::composeDir(), []() { return Kokoro::fromEnv().available(); });
        return outcome.isOk() ? QStringLiteral("Kokoro is running.") : outcome.error().message;
    }));
}

void AiController::onServiceStarted() {
    setServiceMessage(m_serviceWatcher->result());
    setStarting(QString());
    // Re-probe rather than trust the outcome: shows what is actually true
    // now, not what we hoped happened (matches the Rust original's
    // report_service()).
    refresh();
}

void AiController::refresh() {
    QSqlDatabase &db = Database::forCurrentThread().connection();
    setAvailableProviders(detectProviders(db));
    setOnMains(currentPower() == Power::Mains);
}

void AiController::clearAnswer() {
    setAnswer(QString());
    setSources(QString());
    setStatus(QString());
}

void AiController::refreshModels() {
    m_modelsWatcher->setFuture(QtConcurrent::run([]() {
        QSqlDatabase &db = Database::forCurrentThread().connection();
        Ollama provider = localProvider(db);
        return provider.models().join(QLatin1Char('\n'));
    }));
}

void AiController::onModelsReady() {
    setLocalModels(m_modelsWatcher->result());
}

void AiController::setLocalModel(const QString &model) {
    if (model.trimmed().isEmpty())
        return;
    if (m_localModel == model)
        return;

    m_localModel = model;
    emit localModelChanged();

    QSqlDatabase &db = Database::forCurrentThread().connection();
    SettingsRepository settings(db);
    const Result<void> persisted = settings.set(SETTING_LOCAL_MODEL, model);
    if (persisted.isErr())
        qWarning("could not persist AI setting: %s", qUtf8Printable(persisted.error().message));

    refresh();
}

void AiController::setRemoteModel(const QString &model) {
    if (model.trimmed().isEmpty())
        return;
    if (m_remoteModel == model)
        return;

    m_remoteModel = model;
    emit remoteModelChanged();

    QSqlDatabase &db = Database::forCurrentThread().connection();
    SettingsRepository settings(db);
    const Result<void> persisted = settings.set(SETTING_REMOTE_MODEL, model);
    if (persisted.isErr())
        qWarning("could not persist AI setting: %s", qUtf8Printable(persisted.error().message));
}

void AiController::setRemoteKey(const QString &key) {
    const QString trimmed = key.trimmed();

    QSqlDatabase &db = Database::forCurrentThread().connection();
    SettingsRepository settings(db);
    const Result<void> persisted = settings.set(SETTING_REMOTE_KEY, trimmed);
    if (persisted.isErr())
        qWarning("could not persist AI setting: %s", qUtf8Printable(persisted.error().message));

    // Reflect what is actually reachable now, not what was typed: an
    // exported ANTHROPIC_API_KEY can still supply a key after this clears
    // the stored one.
    setHasRemoteKey(remoteKey(db).has_value());
    refresh();
}

void AiController::setBusy(bool busy) {
    if (m_busy == busy)
        return;

    m_busy = busy;
    emit busyChanged();
}

void AiController::setStatus(const QString &status) {
    if (m_status == status)
        return;

    m_status = status;
    emit statusChanged();
}

void AiController::setAnswer(const QString &answer) {
    if (m_answer == answer)
        return;

    m_answer = answer;
    emit answerChanged();
}

void AiController::setSources(const QString &sources) {
    if (m_sources == sources)
        return;

    m_sources = sources;
    emit sourcesChanged();
}

void AiController::setProvider(const QString &provider) {
    if (m_provider == provider)
        return;

    m_provider = provider;
    emit providerChanged();
}

void AiController::setAvailableProviders(const QString &providers) {
    if (m_availableProviders == providers)
        return;

    m_availableProviders = providers;
    emit availableProvidersChanged();
}

void AiController::setIndexing(bool indexing) {
    if (m_indexing == indexing)
        return;

    m_indexing = indexing;
    emit indexingChanged();
}

void AiController::setIndexDone(int done) {
    if (m_indexDone == done)
        return;

    m_indexDone = done;
    emit indexDoneChanged();
}

void AiController::setIndexTotal(int total) {
    if (m_indexTotal == total)
        return;

    m_indexTotal = total;
    emit indexTotalChanged();
}

void AiController::setOnMains(bool onMains) {
    if (m_onMains == onMains)
        return;

    m_onMains = onMains;
    emit onMainsChanged();
}

void AiController::setStarting(const QString &starting) {
    if (m_starting == starting)
        return;

    m_starting = starting;
    emit startingChanged();
}

void AiController::setServiceMessage(const QString &message) {
    if (m_serviceMessage == message)
        return;

    m_serviceMessage = message;
    emit serviceMessageChanged();
}

void AiController::setLocalModels(const QString &models) {
    if (m_localModels == models)
        return;

    m_localModels = models;
    emit localModelsChanged();
}

void AiController::setHasRemoteKey(bool present) {
    if (m_hasRemoteKey == present)
        return;

    m_hasRemoteKey = present;
    emit hasRemoteKeyChanged();
}

void AiController::startIndexRun(const std::function<void(IndexWorker *)> &kickoff) {
    // No parent, or moveToThread silently does nothing (CLAUDE.md, "Traps").
    auto *worker = new IndexWorker;
    auto *thread = new QThread;
    m_indexWorker = worker;
    m_indexThread = thread;
    worker->moveToThread(thread);

    connect(worker, &IndexWorker::statusChanged, this, &AiController::onIndexStatus);
    connect(worker, &IndexWorker::progressChanged, this, &AiController::onIndexProgress);
    connect(worker, &IndexWorker::finished, this, &AiController::onIndexFinished);
    connect(worker, &IndexWorker::finished, thread, &QThread::quit);
    connect(worker, &IndexWorker::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::started, worker, [worker, kickoff]() { kickoff(worker); });

    thread->start();
}
