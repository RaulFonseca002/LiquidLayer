#pragma once

#include "liquid/Ids.hpp"

#include <cstddef>
#include <set>
#include <vector>

namespace liquid::detail {

class IntentRegistry;

class BehaviorRegistry {
private:
    WorldInstanceId worldId = 0;
    std::set<BehaviorId> active;
    std::vector<std::uint16_t> availableSlots;
    std::vector<std::uint32_t> generations;

public:
    explicit BehaviorRegistry(WorldInstanceId world = 0);
    BehaviorId create();
    void destroy(BehaviorId id);
    bool exists(BehaviorId id) const;
    std::size_t size() const;
};

}
