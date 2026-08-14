#pragma once

#include "liquid/Ids.hpp"
#include "liquid/IntentLifetime.hpp"

#include <cstddef>
#include <vector>

namespace liquid {

class World;

std::vector<IntentId> expired_intent_ids(const World& world, IntentTime now);

}
