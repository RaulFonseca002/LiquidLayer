#pragma once

#include "liquid/detail/BehaviorRegistry.hpp"
#include "liquid/detail/ComponentRegistry.hpp"
#include "liquid/detail/IntentRegistry.hpp"
#include "liquid/detail/SystemRegistry.hpp"

#include <map>

namespace liquid::detail {

struct WorldState {
    explicit WorldState(WorldInstanceId instance)
        : instanceId(instance),
          components(instance),
          behaviors(instance),
          intents(instance) {
    }

    WorldInstanceId instanceId;
    ComponentRegistry components;
    BehaviorRegistry behaviors;
    IntentRegistry intents;
    SystemRegistry systems;
    std::map<BehaviorId, Signature> behaviorSignatures;
    std::map<BehaviorId, BehaviorAccessRevision> behaviorAccessRevisions;
    BehaviorAccessRevision lastBehaviorAccessRevision = 0;
};

}
