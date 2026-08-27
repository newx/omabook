#include "core/services.h"

#include <QElapsedTimer>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>

QString StartFailure::message() const {
    switch (m_kind) {
    case StartFailureKind::Missing:
        if (m_detail == QLatin1String("docker"))
            return QStringLiteral("Docker is not installed. Install it, or run Kokoro another way.");
        if (m_detail == QLatin1String("ollama"))
            return QStringLiteral("Ollama is not installed. Install it to use summaries and questions.");
        return QStringLiteral("%1 is not installed.").arg(m_detail);
    case StartFailureKind::Failed:
        return m_detail;
    case StartFailureKind::NeverReady:
        return QStringLiteral("It started but is not answering yet. Give it a moment and check again.");
    }
    Q_UNREACHABLE_RETURN(QString());
}

namespace {

// Every external process gets a bounded wait so a wedged systemctl or
// docker cannot hang whoever is offering to start the service (CLAUDE.md,
// "External processes").
constexpr int PROCESS_TIMEOUT_MS = 15000;
// wait_until's poll interval, matching the Rust original.
constexpr int POLL_INTERVAL_MS = 400;

// Runs a command and turns a non-zero exit -- or a process that could not
// start at all -- into a StartFailure built from its stderr. errorOccurred
// is connected explicitly, not inferred from waitForStarted's return alone,
// because that is the signal a missing binary actually emits (CLAUDE.md,
// "External processes").
std::optional<StartFailure> run(const QString &program, const QStringList &args,
                                 const QString &workingDir = QString()) {
    QProcess process;
    QByteArray errorOutput;
    bool startFailed = false;

    QObject::connect(&process, &QProcess::errorOccurred, [&](QProcess::ProcessError) { startFailed = true; });
    QObject::connect(&process, &QProcess::readyReadStandardError,
                      [&]() { errorOutput += process.readAllStandardError(); });

    if (!workingDir.isEmpty())
        process.setWorkingDirectory(workingDir);

    process.start(program, args);
    if (!process.waitForStarted(PROCESS_TIMEOUT_MS) || startFailed)
        return StartFailure::failed(Services::firstSentence(QString::fromUtf8(errorOutput)));

    if (!process.waitForFinished(PROCESS_TIMEOUT_MS)) {
        process.kill();
        process.waitForFinished(1000);
        return StartFailure::failed(QStringLiteral("timed out waiting for it to finish"));
    }
    errorOutput += process.readAllStandardError();

    if (startFailed)
        return StartFailure::failed(Services::firstSentence(QString::fromUtf8(errorOutput)));
    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0)
        return std::nullopt;

    const QString firstLine = QString::fromUtf8(errorOutput).split(QLatin1Char('\n')).value(0);
    return StartFailure::failed(Services::firstSentence(firstLine));
}

} // namespace

namespace Services {

bool toolInstalled(const QString &tool) {
    return !QStandardPaths::findExecutable(tool).isEmpty();
}

std::optional<StartFailure> waitUntil(std::function<bool()> ready, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (ready())
            return std::nullopt;
        QThread::msleep(POLL_INTERVAL_MS);
    }
    return StartFailure::neverReady();
}

QString firstSentence(const QString &text) {
    QString cleaned = text.trimmed();
    if (cleaned.startsWith(QLatin1String("Error: ")))
        cleaned = cleaned.mid(7).trimmed();
    if (cleaned.isEmpty())
        return QStringLiteral("the command failed");

    if (cleaned.size() > 160)
        return cleaned.left(160) + QStringLiteral("…");
    return cleaned;
}

Result<void> startOllama(std::function<bool()> ready) {
    // A missing tool is reported before anything is run, rather than
    // letting QProcess fail obscurely partway through.
    if (!toolInstalled(QStringLiteral("ollama")))
        return Error::net(StartFailure::missing(QStringLiteral("ollama")).message());

    const std::optional<StartFailure> unitFailure = run(
            QStringLiteral("systemctl"), { QStringLiteral("--user"), QStringLiteral("start"),
                                            QStringLiteral("ollama.service") });
    if (unitFailure) {
        // No unit installed: run the server directly instead of failing.
        // Detached because nothing here should wait on or own an
        // indefinitely-running server process, mirroring the Rust
        // original's fire-and-forget Command::spawn.
        if (!QProcess::startDetached(QStringLiteral("ollama"), { QStringLiteral("serve") })) {
            return Error::net(
                    StartFailure::failed(QStringLiteral("could not start `ollama serve`")).message());
        }
    }

    const std::optional<StartFailure> waitFailure = waitUntil(ready, 20000);
    if (waitFailure)
        return Error::net(waitFailure->message());
    return VoidResult::ok();
}

Result<void> startKokoro(const QString &composeDir, std::function<bool()> ready) {
    if (!toolInstalled(QStringLiteral("docker")))
        return Error::net(StartFailure::missing(QStringLiteral("docker")).message());

    // An existing container is the common case and the quickest path.
    const std::optional<StartFailure> byName =
            run(QStringLiteral("docker"), { QStringLiteral("start"), QStringLiteral("omabook_kokoro") });

    if (byName) {
        if (composeDir.isEmpty()) {
            return Error::net(StartFailure::failed(QStringLiteral(
                                                             "Kokoro has not been created yet. Run "
                                                             "`docker compose up -d kokoro` in the omabook "
                                                             "checkout once."))
                                       .message());
        }
        const std::optional<StartFailure> composeFailure =
                run(QStringLiteral("docker"),
                    { QStringLiteral("compose"), QStringLiteral("up"), QStringLiteral("-d"),
                      QStringLiteral("kokoro") },
                    composeDir);
        if (composeFailure)
            return Error::net(composeFailure->message());
    }

    // The image is large and loads its voices on boot, so this is not quick.
    const std::optional<StartFailure> waitFailure = waitUntil(ready, 90000);
    if (waitFailure)
        return Error::net(waitFailure->message());
    return VoidResult::ok();
}

} // namespace Services
