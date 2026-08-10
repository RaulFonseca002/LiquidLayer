#include "liquid/IntentExpiration.hpp"

#include "liquid/world/Coordinator.hpp"
#include "liquid/IntentRegistry.hpp"
#include "liquid/world/World.hpp"

#include <stdexcept>

namespace liquid {

namespace {

bool lifetime_is_expired(const IntentLifetime& lifetime, IntentTime now) {
    switch (lifetime.kind) {
        case IntentLifetimeKind::Persistent:
            return false;

        case IntentLifetimeKind::UntilTime:
            return now >= lifetime.expiresAt;
    }

    throw std::invalid_argument("unknown intent lifetime kind");
}

}

std::vector<IntentId> expired_intent_ids(const IntentRegistry& intents, IntentTime now) {
    std::vector<IntentId> expired;

    for (IntentId id : intents.live_intent_ids()) {
        if (lifetime_is_expired(intents.intent(id).lifetime, now))
            expired.push_back(id);
    }

    return expired;
}

std::size_t destroy_expired_intents(IntentRegistry& intents, IntentTime now) {
    std::vector<IntentId> expired = expired_intent_ids(intents, now);

    for (IntentId id : expired)
        intents.destroy(id);

    return expired.size();
}

std::vector<IntentId> expired_intent_ids(const Coordinator& coordinator, IntentTime now) {
    std::vector<IntentId> expired;

    for (IntentId id : coordinator.live_intent_ids()) {
        if (lifetime_is_expired(coordinator.intent(id).lifetime, now))
            expired.push_back(id);
    }

    return expired;
}

std::size_t destroy_expired_intents(Coordinator& coordinator, IntentTime now) {
    std::vector<IntentId> expired = expired_intent_ids(coordinator, now);

    for (IntentId id : expired)
        coordinator.destroy_intent(id);

    return expired.size();
}

std::vector<IntentId> expired_intent_ids(const World& world, IntentTime now) {
    std::vector<IntentId> expired;

    for (IntentId id : world.live_intent_ids()) {
        if (lifetime_is_expired(world.intent(id).lifetime, now))
            expired.push_back(id);
    }

    return expired;
}

}
