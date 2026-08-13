#include "liquid/detail/BehaviorRegistry.hpp"

#include <numeric>
#include <limits>
#include <stdexcept>

namespace liquid::detail {

BehaviorRegistry::BehaviorRegistry(WorldInstanceId world)
    : worldId(world),
      generations(MaxBehaviours, 1) {
    availableSlots.resize(MaxBehaviours);
    std::iota(availableSlots.rbegin(), availableSlots.rend(), 0);
}

BehaviorId BehaviorRegistry::create() {
    if (availableSlots.empty())
        throw std::runtime_error("all behavior ids are already in use");


    std::uint16_t slot = availableSlots.back();
    availableSlots.pop_back();
    BehaviorId behavior{worldId, slot, generations.at(slot)};

    try {
        auto [position, inserted] = active.emplace(behavior);
        (void)position;

        if (!inserted)
            throw std::logic_error("behavior id already active");
    } catch (...) {
        availableSlots.push_back(slot);
        throw;
    }

    return behavior;
}

void BehaviorRegistry::destroy(BehaviorId id) {
    if (!exists(id))
        throw std::runtime_error("behavior id not found");

    active.erase(id);

    if (generations.at(id.slot) == std::numeric_limits<std::uint32_t>::max())
        return;

    ++generations.at(id.slot);
    availableSlots.push_back(id.slot);
}

bool BehaviorRegistry::exists(BehaviorId id) const {
    return active.find(id) != active.end();
}

std::size_t BehaviorRegistry::size() const {
    return active.size();
}

}
