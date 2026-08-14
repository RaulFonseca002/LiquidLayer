#pragma once

#include "liquid/IntentExpiration.hpp"

namespace liquid::detail {

class Coordinator;
class IntentRegistry;

std::vector<IntentId> expired_intent_ids(
    const IntentRegistry& intents, IntentTime now);
std::size_t destroy_expired_intents(IntentRegistry& intents, IntentTime now);
std::vector<IntentId> expired_intent_ids(
    const Coordinator& coordinator, IntentTime now);
std::size_t destroy_expired_intents(Coordinator& coordinator, IntentTime now);

}
