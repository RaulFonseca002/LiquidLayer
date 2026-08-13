#include "liquid/world/World.hpp"

#include "liquid/detail/IntentExpiration.hpp"

#include <atomic>
#include <limits>
#include <stdexcept>

namespace liquid {

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
    : state(next_world_instance_id()),
      coordinator(state),
      ownerThread(std::this_thread::get_id())
{
}

BehaviorId World::create_behavior() {
    ensure_owner_thread();
    ensure_structural_mutation_allowed();
    const BehaviorId behavior = coordinator.create_behavior();
    Value::Object value;
    value.emplace("world", Value{behavior.world});
    value.emplace("slot", Value{static_cast<std::uint64_t>(behavior.slot)});
    value.emplace("generation", Value{static_cast<std::uint64_t>(behavior.generation)});
    record_topology(
        "behavior:" + std::to_string(behavior.world) + ":" +
            std::to_string(behavior.slot) + ":" +
            std::to_string(behavior.generation),
        Value{std::move(value)});
    return behavior;
}

void World::destroy_behavior(BehaviorId id) {
    ensure_owner_thread();
    ensure_structural_mutation_allowed();
    coordinator.destroy_behavior(id);
    record_topology(
        "behavior:" + std::to_string(id.world) + ":" +
            std::to_string(id.slot) + ":" + std::to_string(id.generation),
        Value{}, true);
}

bool World::behavior_exists(BehaviorId id) {
    ensure_owner_thread();
    return coordinator.behavior_exists(id);
}

std::size_t World::behavior_count() {
    ensure_owner_thread();
    return coordinator.behavior_count();
}

WorldInstanceId World::instance_id() const {
    ensure_owner_thread();
    return state.instanceId;
}

BehaviorAccessRevision World::behavior_access_revision(BehaviorId id) const {
    ensure_owner_thread();
    return coordinator.behavior_access_revision(id);
}

void World::destroy_intent(IntentId id) {
    ensure_owner_thread();
    coordinator.destroy_intent(id);
}

bool World::intent_exists(IntentId id) {
    ensure_owner_thread();
    return coordinator.intent_exists(id);
}

BehaviorId World::intent_owner(IntentId id) {
    ensure_owner_thread();
    return coordinator.intent_owner(id);
}

ComponentTarget World::intent_target(IntentId id) {
    ensure_owner_thread();
    return coordinator.intent_target(id);
}

IntentLifetime World::intent_lifetime(IntentId id) {
    ensure_owner_thread();
    return coordinator.intent_lifetime(id);
}

const Intent& World::intent(IntentId id) const {
    ensure_owner_thread();
    return coordinator.intent(id);
}

std::vector<IntentId> World::live_intent_ids() const {
    ensure_owner_thread();
    return coordinator.live_intent_ids();
}

std::vector<IntentId> World::intents_for(ComponentTypeId type, ComponentSlotId slot) const {
    ensure_owner_thread();
    return coordinator.intents_for(type, slot);
}

const IntentTargetIndex& World::intent_target_index() const {
    ensure_owner_thread();
    return coordinator.intent_target_index();
}

const std::vector<detail::IntentLifecycleRecord>& World::intent_lifecycle_records() const {
    ensure_owner_thread();
    return coordinator.intent_lifecycle_records();
}

void World::clear_intent_lifecycle_records() {
    ensure_owner_thread();
    coordinator.clear_intent_lifecycle_records();
}

bool World::resolution_request_is_current(
    ComponentTypeId type,
    const std::map<ComponentName, ComponentSlotId>& components
) const {
    ensure_owner_thread();
    return coordinator.resolution_request_is_current(type, components);
}

std::size_t World::destroy_expired_intents(IntentTime now) {
    ensure_owner_thread();
    return detail::destroy_expired_intents(coordinator, now);
}

std::map<ComponentName, IntentId> World::resolve_intents(
    ComponentTypeId type,
    const std::map<ComponentName, ComponentSlotId>& components,
    IntentTime now
) {
    ensure_owner_thread();
    if (!resolution_request_is_current(type, components))
        throw std::invalid_argument("intent resolution request contains a stale component target");

    return coordinator.resolve_intents(type, components, now);
}

std::size_t World::intent_count(BehaviorId owner) {
    ensure_owner_thread();
    return coordinator.intent_count(owner);
}

std::size_t World::system_count() const {
    ensure_owner_thread();
    return coordinator.system_count();
}

std::size_t World::run_systems(
    FrameNumber frame,
    IntentTime now,
    std::size_t* completedSystems,
    std::string* failingSystem
) {
    ensure_owner_thread();
    if (systemsRunning)
        throw std::logic_error("world system execution is not reentrant");

    systemsRunning = true;

    try {
        std::size_t systemsRun = coordinator.run_systems(
            *this, frame, now, completedSystems, failingSystem);
        systemsRunning = false;
        return systemsRun;
    } catch (...) {
        systemsRunning = false;
        throw;
    }
}

void World::ensure_structural_mutation_allowed() const {
    ensure_owner_thread();
    if (systemsRunning || coordinator.system_membership_dispatching())
        throw std::logic_error("world topology cannot change during system dispatch");
}

void World::ensure_owner_thread() const {
    if (std::this_thread::get_id() != ownerThread)
        throw std::logic_error("world used from a non-owner thread");
}

ComponentTypeId World::component_type(const TypeName& typeName) const {
    ensure_owner_thread();
    return coordinator.component_type(typeName);
}

Signature World::behavior_signature(BehaviorId behavior) const {
    ensure_owner_thread();
    return coordinator.behavior_signature(behavior);
}

const std::vector<liquid::ComponentMutation>& World::component_mutations() const {
    ensure_owner_thread();
    return componentMutations;
}

void World::clear_component_mutations() {
    ensure_owner_thread();
    componentMutations.clear();
}

void World::record_topology(std::string key, Value value, bool removed) {
    constexpr std::size_t maximumBufferedWorldEvidence = 1'000'000;
    if (topologyMutations.size() >= maximumBufferedWorldEvidence)
        throw std::length_error("topology evidence capacity exceeded");
    topologyMutations.push_back(
        TopologyMutation{std::move(key), std::move(value), removed});
}

void World::record_component_mutation(ComponentMutation mutation) {
    constexpr std::size_t maximumBufferedWorldEvidence = 1'000'000;
    if (componentMutations.size() >= maximumBufferedWorldEvidence)
        throw std::length_error("component evidence capacity exceeded");
    componentMutations.push_back(std::move(mutation));
}

void World::enable_script_evidence() {
    ensure_owner_thread();
    scriptEvidenceEnabled = true;
}

void World::record_script_execution(ScriptExecutionEvidence evidence) {
    ensure_owner_thread();
    if (!scriptEvidenceEnabled)
        return;
    constexpr std::size_t maximumBufferedScriptExecutions = 4096;
    if (scriptExecutions.size() >= maximumBufferedScriptExecutions)
        throw std::length_error("script execution evidence capacity exceeded");
    scriptExecutions.push_back(std::move(evidence));
}

const std::vector<TopologyMutation>& World::topology_mutations() const {
    ensure_owner_thread();
    return topologyMutations;
}

void World::clear_topology_mutations() {
    ensure_owner_thread();
    topologyMutations.clear();
}

const std::vector<ScriptExecutionEvidence>& World::script_execution_evidence() const {
    ensure_owner_thread();
    return scriptExecutions;
}

void World::clear_script_execution_evidence() {
    ensure_owner_thread();
    scriptExecutions.clear();
}

}
