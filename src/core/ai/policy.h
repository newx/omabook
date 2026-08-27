// What AI work is allowed to run, and when. Ported from
// omabook-core/src/ai/policy.rs.
//
// The problem this solves: a remote provider bills per token, and a laptop
// has a battery. Precomputing summaries for a whole library is therefore
// not a neutral background convenience -- it can be expensive in money, in
// heat, and in charge.
//
// The rule that makes cost predictable:
//
//   Background work never uses a remote provider.
//
// With that in place, a bill can only grow because someone clicked
// something and waited for the answer. This is the one place that rule
// lives (CLAUDE.md, "Optional services") -- every call site asks
// WorkPolicy::permits() rather than deciding for itself.
#pragma once

#include "core/ai/power.h"
#include "core/ai/provider.h"

#include <QString>

// Who asked for the work.
enum class Trigger {
    Interactive, // The user clicked, and is waiting for the result.
    Background,  // Nobody is waiting. Queued work, prefetching, maintenance.
};

// What the policy decided, and why. The reason is shown to the user rather
// than logged and forgotten -- silent refusal is worse than no feature.
class Decision {
public:
    enum class Kind { Allow, Defer, Refuse };

    static Decision allow() { return Decision(Kind::Allow, QString()); }
    // Not now, but it would run under different conditions.
    static Decision defer(const QString &reason) { return Decision(Kind::Defer, reason); }
    // Never in this mode.
    static Decision refuse(const QString &reason) { return Decision(Kind::Refuse, reason); }

    Kind kind() const { return m_kind; }
    bool isAllowed() const { return m_kind == Kind::Allow; }
    QString reason() const { return m_reason; }

    bool operator==(const Decision &other) const {
        return m_kind == other.m_kind && m_reason == other.m_reason;
    }
    bool operator!=(const Decision &other) const { return !(*this == other); }

private:
    Decision(Kind kind, const QString &reason) : m_kind(kind), m_reason(reason) { }

    Kind m_kind;
    QString m_reason;
};

// User-controlled settings for background AI work. Everything defaults to
// the cautious answer.
struct WorkPolicy {
    bool backgroundEnabled = false;   // Whether background indexing may run at all. Off until asked for.
    bool backgroundOnBattery = false; // Whether background work may run on battery.

    // Evaluated in exactly this order (SPEC §5.5):
    //   1. Interactive -> Allow, unconditionally.
    //   2. Remote -> Refuse; background work never costs money.
    //   3. generative task -> Refuse; summaries are never precomputed.
    //   4. background disabled -> Refuse.
    //   5. battery and not permitted on battery -> Defer.
    //   6. otherwise -> Allow.
    Decision permits(TaskKind task, Trigger trigger, ProviderClass provider, Power power) const;
};
