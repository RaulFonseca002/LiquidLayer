#pragma once

#include "liquid/Ids.hpp"
#include "liquid/IntentLifetime.hpp"
#include "liquid/Value.hpp"

#include <map>
#include <set>
#include <string>

namespace liquid {

using IntentName = std::string;

struct Intent {
    IntentId id = 0;
    BehaviorId owner = 0;
    ComponentTarget target;
    IntentLifetime lifetime;
    IntentPriority priority = IntentPriority::Medium;
    IntentSequence sequence = 0;
    Value encodedValue;
    IntentName name;
};

template <typename Component>
struct ComponentIntent : Intent {
    Component value;
};

using IntentTargetIndex =
    std::map<ComponentTypeId, std::map<ComponentSlotId, std::set<IntentId>>>;

}
