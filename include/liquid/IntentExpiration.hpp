#pragma once

#include "liquid/Ids.hpp"
#include "liquid/IntentLifetime.hpp"

#include <cstddef>
#include <vector>

class Coordinator;
class IntentRegistry;
class World;

namespace liquid {

std::vector<IntentId> expired_intent_ids(const IntentRegistry& intents, IntentTime now);
std::size_t destroy_expired_intents(IntentRegistry& intents, IntentTime now);
std::vector<IntentId> expired_intent_ids(const Coordinator& coordinator, IntentTime now);
std::size_t destroy_expired_intents(Coordinator& coordinator, IntentTime now);
std::vector<IntentId> expired_intent_ids(const World& world, IntentTime now);

}

using liquid::destroy_expired_intents;
using liquid::expired_intent_ids;
