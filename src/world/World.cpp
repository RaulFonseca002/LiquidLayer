#include "liquid/world/World.hpp"

#include "liquid/IntentExpiration.hpp"

#include <atomic>
#include <limits>
#include <stdexcept>

namespace {

WorldInstanceId next_world_instance_id() {
    static std::atomic<WorldInstanceId> next{1};
    WorldInstanceId candidate = next.load(std::memory_order_relaxed);

    while (true) {
        if (candidate == std::numeric_limits<WorldInstanceId>::max())
            throw std::overflow_error("world instance id space exhausted");

        if (next.compare_exchange_weak(
                candidate,
                candidate + 1,
                std::memory_order_relaxed,
                std::memory_order_relaxed
            ))
            return candidate;
    }
}

}

World::World()
    : state(),
      coordinator(state)
{
    state.instanceId = next_world_instance_id();
}

BehaviorId World::create_behavior() {
    ensure_structural_mutation_allowed();
    return coordinator.create_behavior();
}

void World::destroy_behavior(BehaviorId id) {
    ensure_structural_mutation_allowed();
    coordinator.destroy_behavior(id);
}

bool World::behavior_exists(BehaviorId id) {
    return coordinator.behavior_exists(id);
}

std::size_t World::behavior_count() {
    return coordinator.behavior_count();
}

WorldInstanceId World::instance_id() const {
    return state.instanceId;
}

BehaviorAccessRevision World::behavior_access_revision(BehaviorId id) const {
    return coordinator.behavior_access_revision(id);
}

void World::destroy_intent(IntentId id) {
    coordinator.destroy_intent(id);
}

bool World::intent_exists(IntentId id) {
    return coordinator.intent_exists(id);
}

BehaviorId World::intent_owner(IntentId id) {
    return coordinator.intent_owner(id);
}

ComponentTarget World::intent_target(IntentId id) {
    return coordinator.intent_target(id);
}

IntentLifetime World::intent_lifetime(IntentId id) {
    return coordinator.intent_lifetime(id);
}

const Intent& World::intent(IntentId id) const {
    return coordinator.intent(id);
}

std::vector<IntentId> World::live_intent_ids() const {
    return coordinator.live_intent_ids();
}

std::vector<IntentId> World::intents_for(ComponentTypeId type, ComponentSlotId slot) const {
    return coordinator.intents_for(type, slot);
}

const IntentTargetIndex& World::intent_target_index() const {
    return coordinator.intent_target_index();
}

bool World::resolution_request_is_current(
    ComponentTypeId type,
    const std::map<ComponentName, ComponentSlotId>& components
) const {
    return coordinator.resolution_request_is_current(type, components);
}

std::size_t World::destroy_expired_intents(IntentTime now) {
    return liquid::destroy_expired_intents(coordinator, now);
}

std::map<ComponentName, IntentId> World::resolve_intents(
    ComponentTypeId type,
    const std::map<ComponentName, ComponentSlotId>& components,
    IntentTime now
) {
    if (!resolution_request_is_current(type, components))
        throw std::invalid_argument("intent resolution request contains a stale component target");

    return coordinator.resolve_intents(type, components, now);
}

std::size_t World::intent_count(BehaviorId owner) {
    return coordinator.intent_count(owner);
}

std::size_t World::system_count() const {
    return coordinator.system_count();
}

std::size_t World::run_systems(
    FrameNumber frame,
    IntentTime now,
    std::size_t* completedSystems
) {
    if (systemsRunning)
        throw std::logic_error("world system execution is not reentrant");

    systemsRunning = true;

    try {
        std::size_t systemsRun = coordinator.run_systems(*this, frame, now, completedSystems);
        systemsRunning = false;
        return systemsRun;
    } catch (...) {
        systemsRunning = false;
        throw;
    }
}

void World::ensure_structural_mutation_allowed() const {
    if (systemsRunning || coordinator.system_membership_dispatching())
        throw std::logic_error("world topology cannot change during system dispatch");
}

ComponentTypeId World::component_type(const TypeName& typeName) const {
    return coordinator.component_type(typeName);
}

Signature World::behavior_signature(BehaviorId behavior) const {
    return coordinator.behavior_signature(behavior);
}
