// What a speech backend must do, ported from the `SpeechBackend` trait in
// omabook-core/src/tts/mod.rs. One interface so the app never cares which
// engine is behind it, and so an unavailable engine degrades rather than
// breaking the reader (SPEC §6.2).
#pragma once

#include "core/result.h"

#include <QString>

class SpeechBackend {
public:
    virtual ~SpeechBackend() = default;

    // Synthesize `text`, returning the path to a playable WAV file.
    virtual Result<QString> synthesize(const QString &text) = 0;

    // Whether the backend can be reached right now.
    virtual bool available() const = 0;

    virtual QString name() const = 0;
};
