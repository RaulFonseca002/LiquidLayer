#include "liquid/BehaviorRegistry.hpp"

#include <numeric>
#include <stdexcept>

BehaviorRegistry::BehaviorRegistry() {
    availableIds.resize(MAX_BEHAVIOURS);
    std::iota(availableIds.rbegin(), availableIds.rend(), 0);
}

BehaviorId BehaviorRegistry::create() {
    if (availableIds.empty())
        throw std::runtime_error("all behavior ids are already in use");


    BehaviorId behavior = availableIds.back();
    availableIds.pop_back();

    try {
        auto [position, inserted] = active.emplace(behavior);
        (void)position;

        if (!inserted)
            throw std::logic_error("behavior id already active");
    } catch (...) {
        availableIds.push_back(behavior);
        throw;
    }

    return behavior;
}

void BehaviorRegistry::destroy(BehaviorId id) {
    if (!exists(id))
        throw std::runtime_error("behavior id not found");

    active.erase(id);
    availableIds.push_back(id);
}

bool BehaviorRegistry::exists(BehaviorId id) const {
    return active.find(id) != active.end();
}

std::size_t BehaviorRegistry::size() const {
    return active.size();
}
