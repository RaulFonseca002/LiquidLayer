#pragma once

#include <cstdint>

namespace liquid {

// Monotonic milliseconds since the current runtime session began. It is not
// wall-clock or epoch time and must never move backwards between frames.
using IntentTime = std::uint64_t;

enum class IntentLifetimeKind {
    Persistent,
    UntilTime
};

struct IntentLifetime {
    IntentLifetime() = delete;

    IntentLifetimeKind kind;
    IntentTime expiresAt;

    IntentLifetime(IntentLifetimeKind lifetimeKind, IntentTime expirationTime)
        : kind(lifetimeKind),
          expiresAt(expirationTime)
    {
    }

    static IntentLifetime persistent() {
        return {IntentLifetimeKind::Persistent, 0};
    }

    static IntentLifetime until_time(IntentTime time) {
        return {IntentLifetimeKind::UntilTime, time};
    }
};

}

using liquid::IntentLifetime;
using liquid::IntentLifetimeKind;
using liquid::IntentTime;
