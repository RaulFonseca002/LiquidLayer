#pragma once

#include "liquid/world/Coordinator.hpp"
#include "liquid/Ids.hpp"
#include "liquid/IntentLifetime.hpp"
#include "liquid/world/WorldState.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

class Runtime;

// World and Runtime are single-thread-confined. A World owns all handles and
// borrowed component views for one runtime session; callers must synchronize
// externally and must not expose this trusted host boundary to scripts.
class World {
private:
    friend class Runtime;

    WorldState state;
    Coordinator coordinator;
    bool systemsRunning = false;

    void ensure_structural_mutation_allowed() const;
    bool resolution_request_is_current(
        ComponentTypeId type,
        const std::map<ComponentName, ComponentSlotId>& components
    ) const;
    std::size_t destroy_expired_intents(IntentTime now);
    std::map<ComponentName, IntentId> resolve_intents(
        ComponentTypeId type,
        const std::map<ComponentName, ComponentSlotId>& components,
        IntentTime now
    );
    std::size_t run_systems(
        FrameNumber frame,
        IntentTime now,
        std::size_t* completedSystems = nullptr
    );

public:
    World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) = delete;
    World& operator=(World&&) = delete;

    BehaviorId create_behavior();
    void destroy_behavior(BehaviorId id);
    bool behavior_exists(BehaviorId id);
    std::size_t behavior_count();
    WorldInstanceId instance_id() const;
    BehaviorAccessRevision behavior_access_revision(BehaviorId id) const;

    void destroy_intent(IntentId id);
    bool intent_exists(IntentId id);
    BehaviorId intent_owner(IntentId id);
    ComponentTarget intent_target(IntentId id);
    IntentLifetime intent_lifetime(IntentId id);
    const Intent& intent(IntentId id) const;
    std::vector<IntentId> live_intent_ids() const;
    std::vector<IntentId> intents_for(ComponentTypeId type, ComponentSlotId slot) const;
    const IntentTargetIndex& intent_target_index() const;
    std::size_t intent_count(BehaviorId owner);

    template <typename SystemType, typename... Args>
    void register_system(Signature signature, Args&&... args);

    template <typename SystemType>
    void destroy_system();

    template <typename SystemType>
    bool system_exists() const;

    std::size_t system_count() const;

    template <typename SystemType>
    void set_system_signature(Signature signature);

    template <typename SystemType>
    Signature system_signature() const;

    template <typename SystemType>
    SystemType& get_system();

    template <typename SystemType>
    const SystemType& get_system() const;

    template <typename SystemType>
    bool system_has_behavior(BehaviorId behavior) const;

    template <typename SystemType>
    std::size_t system_behavior_count() const;

    template <typename Component>
    ComponentType<Component> register_component(TypeName typeName);

    ComponentTypeId component_type(const TypeName& typeName) const;

    template <typename Component>
    ComponentTypeId component_type(ComponentType<Component> type) const;

    template <typename Component>
    void add_component(ComponentType<Component> type, std::string name, Component component);

    template <typename Component>
    bool has_component_named(ComponentType<Component> type, const std::string& name) const;

    template <typename Component>
    Component* get_component_named(ComponentType<Component> type, const std::string& name);

    template <typename Component>
    const Component* get_component_named(ComponentType<Component> type, const std::string& name) const;

    template <typename Component>
    void remove_component(ComponentType<Component> type, const std::string& name);

    template <typename Component>
    void grant_component_access(
        ComponentType<Component> type,
        BehaviorId behavior,
        const std::string& name,
        ComponentAccessMode mode
    );

    template <typename Component>
    void revoke_component_access(ComponentType<Component> type, BehaviorId behavior, const std::string& name);

    template <typename Component>
    std::map<std::string, ComponentSlotId> get_components(ComponentType<Component> type, BehaviorId behavior) const;

    template <typename Component>
    const Component* read_component(ComponentType<Component> type, BehaviorId behavior, const std::string& name) const;

    template <typename Component>
    Component* write_component(ComponentType<Component> type, BehaviorId behavior, const std::string& name);

    template <typename Component>
    Component* resolve_component(ComponentType<Component> type, ComponentSlotId slot);

    template <typename Component>
    const Component* resolve_component(ComponentType<Component> type, ComponentSlotId slot) const;

    template <typename Component>
    IntentId create_intent(
        BehaviorId owner,
        ComponentType<Component> type,
        ComponentSlotId slot,
        IntentLifetime lifetime,
        Component value,
        IntentPriority priority = IntentPriority::Medium
    );

    template <typename Component>
    const ComponentIntent<Component>& typed_intent(ComponentType<Component> type, IntentId id) const;

    template <typename Component>
    bool can_read_component(ComponentType<Component> type, BehaviorId behavior, const std::string& name) const;

    template <typename Component>
    bool can_write_component(ComponentType<Component> type, BehaviorId behavior, const std::string& name) const;

    Signature behavior_signature(BehaviorId behavior) const;
};

template <typename SystemType, typename... Args>
void World::register_system(Signature signature, Args&&... args) {
    ensure_structural_mutation_allowed();
    coordinator.register_system<SystemType>(signature, std::forward<Args>(args)...);
}

template <typename SystemType>
void World::destroy_system() {
    ensure_structural_mutation_allowed();
    coordinator.destroy_system<SystemType>();
}

template <typename SystemType>
bool World::system_exists() const {
    return coordinator.system_exists<SystemType>();
}

template <typename SystemType>
void World::set_system_signature(Signature signature) {
    ensure_structural_mutation_allowed();
    coordinator.set_system_signature<SystemType>(signature);
}

template <typename SystemType>
Signature World::system_signature() const {
    return coordinator.system_signature<SystemType>();
}

template <typename SystemType>
SystemType& World::get_system() {
    return coordinator.get_system<SystemType>();
}

template <typename SystemType>
const SystemType& World::get_system() const {
    return coordinator.get_system<SystemType>();
}

template <typename SystemType>
bool World::system_has_behavior(BehaviorId behavior) const {
    return coordinator.system_has_behavior<SystemType>(behavior);
}

template <typename SystemType>
std::size_t World::system_behavior_count() const {
    return coordinator.system_behavior_count<SystemType>();
}

template <typename Component>
ComponentType<Component> World::register_component(TypeName typeName) {
    ensure_structural_mutation_allowed();
    return coordinator.register_component<Component>(std::move(typeName));
}

template <typename Component>
ComponentTypeId World::component_type(ComponentType<Component> type) const {
    return coordinator.component_type(type);
}

template <typename Component>
void World::add_component(ComponentType<Component> type, std::string name, Component component) {
    ensure_structural_mutation_allowed();
    coordinator.add_component(type, std::move(name), std::move(component));
}

template <typename Component>
bool World::has_component_named(ComponentType<Component> type, const std::string& name) const {
    return coordinator.has_component_named(type, name);
}

template <typename Component>
Component* World::get_component_named(ComponentType<Component> type, const std::string& name) {
    return coordinator.get_component_named(type, name);
}

template <typename Component>
const Component* World::get_component_named(ComponentType<Component> type, const std::string& name) const {
    return coordinator.get_component_named(type, name);
}

template <typename Component>
void World::remove_component(ComponentType<Component> type, const std::string& name) {
    ensure_structural_mutation_allowed();
    coordinator.remove_component(type, name);
}

template <typename Component>
void World::grant_component_access(
    ComponentType<Component> type,
    BehaviorId behavior,
    const std::string& name,
    ComponentAccessMode mode
) {
    ensure_structural_mutation_allowed();
    coordinator.grant_component_access(type, behavior, name, mode);
}

template <typename Component>
void World::revoke_component_access(ComponentType<Component> type, BehaviorId behavior, const std::string& name) {
    ensure_structural_mutation_allowed();
    coordinator.revoke_component_access(type, behavior, name);
}

template <typename Component>
std::map<std::string, ComponentSlotId> World::get_components(ComponentType<Component> type, BehaviorId behavior) const {
    return coordinator.get_components(type, behavior);
}

template <typename Component>
const Component* World::read_component(ComponentType<Component> type, BehaviorId behavior, const std::string& name) const {
    return coordinator.read_component(type, behavior, name);
}

template <typename Component>
Component* World::write_component(ComponentType<Component> type, BehaviorId behavior, const std::string& name) {
    return coordinator.write_component(type, behavior, name);
}

template <typename Component>
Component* World::resolve_component(ComponentType<Component> type, ComponentSlotId slot) {
    return coordinator.resolve_component(type, slot);
}

template <typename Component>
const Component* World::resolve_component(ComponentType<Component> type, ComponentSlotId slot) const {
    return coordinator.resolve_component(type, slot);
}

template <typename Component>
IntentId World::create_intent(
    BehaviorId owner,
    ComponentType<Component> type,
    ComponentSlotId slot,
    IntentLifetime lifetime,
    Component value,
    IntentPriority priority
) {
    return coordinator.create_intent(owner, type, slot, lifetime, std::move(value), priority);
}

template <typename Component>
const ComponentIntent<Component>& World::typed_intent(ComponentType<Component> type, IntentId id) const {
    return coordinator.typed_intent(type, id);
}

template <typename Component>
bool World::can_read_component(ComponentType<Component> type, BehaviorId behavior, const std::string& name) const {
    return coordinator.can_read_component(type, behavior, name);
}

template <typename Component>
bool World::can_write_component(ComponentType<Component> type, BehaviorId behavior, const std::string& name) const {
    return coordinator.can_write_component(type, behavior, name);
}
