#include "core/ai/policy.h"

Decision WorkPolicy::permits(TaskKind task, Trigger trigger, ProviderClass provider, Power power) const {
    // Someone is waiting for this and chose to ask for it. Their call.
    if (trigger == Trigger::Interactive)
        return Decision::allow();

    // The rule everything else rests on.
    if (provider == ProviderClass::Remote) {
        return Decision::refuse(QStringLiteral(
            "background work never uses a remote provider, so it cannot cost money"));
    }

    // Generating prose for a whole library is expensive even locally, and
    // nobody asked for it. Summaries are produced when requested and then
    // cached forever.
    if (isGenerative(task))
        return Decision::refuse(QStringLiteral("summaries are generated on request, never in bulk"));

    if (!backgroundEnabled)
        return Decision::refuse(QStringLiteral("background indexing is switched off"));

    if (power == Power::Battery && !backgroundOnBattery)
        return Decision::defer(QStringLiteral("waiting for mains power"));

    return Decision::allow();
}
