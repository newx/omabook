// Starting the optional local services, ported from
// omabook-core/src/services.rs.
//
// omabook needs neither of these: the library, reader, search, highlights
// and import all work with both stopped. But when someone opens Settings
// and sees "not running", telling them a command to type is a poor answer
// when we can simply offer to run it.
//
// Both are started the way this machine already runs them: Ollama as a user
// systemd unit, Kokoro as a container from compose.yml. Neither needs root.
#pragma once

#include "core/result.h"

#include <QString>

#include <functional>
#include <optional>

// Why a service could not be started. Each maps to something the person can
// actually do, which is the point of separating them.
enum class StartFailureKind { Missing, Failed, NeverReady };

// A start failure, and the sentence the UI shows for it.
class StartFailure {
public:
    // The tool is not installed. `tool` is "docker" or "ollama" for the two
    // callers here, but any name is accepted.
    static StartFailure missing(const QString &tool) { return StartFailure(StartFailureKind::Missing, tool); }
    // The tool ran and refused; `detail` is what it said.
    static StartFailure failed(const QString &detail) { return StartFailure(StartFailureKind::Failed, detail); }
    // It started, but never became reachable within the deadline.
    static StartFailure neverReady() { return StartFailure(StartFailureKind::NeverReady, QString()); }

    StartFailureKind kind() const { return m_kind; }

    // A sentence for the UI: what went wrong, and what to do next.
    QString message() const;

private:
    StartFailure(StartFailureKind kind, const QString &detail) : m_kind(kind), m_detail(detail) { }

    StartFailureKind m_kind;
    QString m_detail;
};

namespace Services {

// Start Ollama through its user unit, falling back to running it directly.
//
// The packaged *system* unit is deliberately not used: it runs as its own
// user with OLLAMA_MODELS=/var/lib/ollama and cannot see models pulled into
// ~/.ollama, so it starts cleanly and then fails every request.
Result<void> startOllama(std::function<bool()> ready);

// Start the Kokoro container. `composeDir` is where compose.yml lives, when
// it can be found -- empty when not, which is the flattened-Option
// convention this codebase uses elsewhere (CLAUDE.md via models/book.h). An
// installed copy has no repository beside it, so the container is started
// by name in that case, which works once it has been created.
Result<void> startKokoro(const QString &composeDir, std::function<bool()> ready);

// --- Pure/testable logic below, reachable without spawning anything -------

// Whether a program is on PATH. QStandardPaths::findExecutable needs no
// shell and cannot itself run the tool, so a missing tool is reported
// before anything is started.
bool toolInstalled(const QString &tool);

// Poll `ready` until it says yes, or give up after `timeoutMs` and report
// NeverReady. Returns as soon as the predicate is true rather than sleeping
// out the full timeout.
std::optional<StartFailure> waitUntil(std::function<bool()> ready, int timeoutMs);

// Trim a shell's error to something a person reads, without losing the
// point: strips a leading "Error: ", defaults blank input to "the command
// failed", and truncates long text to at most 161 characters with an
// ellipsis.
QString firstSentence(const QString &text);

} // namespace Services
