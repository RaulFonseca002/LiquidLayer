#pragma once

#include "liquid/Ids.hpp"
#include "liquid/world/WorldState.hpp"

#include <cstddef>
#include <exception>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

class World;

class Coordinator {
private:
    friend class World;

    WorldState& state;

    explicit Coordinator(WorldState& state);
    void update_system_memberships(BehaviorId behavior);
    void update_all_system_memberships();
    bool system_membership_dispatching() const;
    void destroy_intents_for_target(ComponentTypeId type, ComponentSlotId slot);
    void destroy_intents_for_owner_target(BehaviorId owner, ComponentTypeId type, ComponentSlotId slot);
    BehaviorAccessRevision next_behavior_access_revision();

public:
    Coordinator(const Coordinator&) = delete;
    Coordinator& operator=(const Coordinator&) = delete;
    Coordinator(Coordinator&&) = delete;
    Coordinator& operator=(Coordinator&&) = delete;

    BehaviorId create_behavior();
    void destroy_behavior(BehaviorId id);
    bool behavior_exists(BehaviorId id);
    std::size_t behavior_count();
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
    std::map<ComponentName, IntentId> resolve_intents(
        ComponentTypeId type,
        const std::map<ComponentName, ComponentSlotId>& components,
        IntentTime now
    );
    std::size_t intent_count(BehaviorId owner);

    template <typename SystemType, typename... Args>
    void register_system(Signature signature, Args&&... args);

    template <typename SystemType>
    void destroy_system();

    template <typename SystemType>
    bool system_exists() const;

    std::size_t system_count() const;
    std::size_t run_systems(
        World& world,
        FrameNumber frame,
        IntentTime now,
        std::size_t* completedSystems = nullptr
    );

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
    bool resolution_request_is_current(
        ComponentTypeId type,
        const std::map<ComponentName, ComponentSlotId>& components
    ) const;

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
    void grant_component_access(ComponentType<Component> type, BehaviorId behavior, const std::string& name, ComponentAccessMode mode);

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
    IntentId create_intent(BehaviorId owner, ComponentType<Component> type, ComponentSlotId slot, IntentLifetime lifetime, Component value, IntentPriority priority = IntentPriority::Medium);

    template <typename Component>
    const ComponentIntent<Component>& typed_intent(ComponentType<Component> type, IntentId id) const;

    template <typename Component>
    bool can_read_component(ComponentType<Component> type, BehaviorId behavior, const std::string& name) const;

    template <typename Component>
    bool can_write_component(ComponentType<Component> type, BehaviorId behavior, const std::string& name) const;

    Signature behavior_signature(BehaviorId behavior) const;
};

template <typename Component>
ComponentType<Component> Coordinator::register_component(TypeName typeName) {
    return state.components.register_component<Component>(std::move(typeName));
}

inline ComponentTypeId Coordinator::component_type(const TypeName& typeName) const {
    return state.components.component_type(typeName);
}

inline bool Coordinator::resolution_request_is_current(
    ComponentTypeId type,
    const std::map<ComponentName, ComponentSlotId>& components
) const {
    if (!state.components.component_type_exists(type))
        return false;

    for (const auto& [name, slot] : components) {
        if (!state.components.component_matches(type, name, slot))
            return false;
    }

    return true;
}

inline bool Coordinator::system_membership_dispatching() const {
    return state.systems.dispatching();
}

template <typename SystemType, typename... Args>
void Coordinator::register_system(Signature signature, Args&&... args) {
    state.systems.register_system<SystemType>(signature, std::forward<Args>(args)...);

    try {
        update_all_system_memberships();
    } catch (...) {
        state.systems.destroy_system<SystemType>();
        throw;
    }
}

template <typename SystemType>
void Coordinator::destroy_system() {
    state.systems.destroy_system<SystemType>();
}

template <typename SystemType>
bool Coordinator::system_exists() const {
    return state.systems.exists<SystemType>();
}

template <typename SystemType>
void Coordinator::set_system_signature(Signature signature) {
    state.systems.set_signature<SystemType>(signature);
    update_all_system_memberships();
}

template <typename SystemType>
Signature Coordinator::system_signature() const {
    return state.systems.signature<SystemType>();
}

template <typename SystemType>
SystemType& Coordinator::get_system() {
    return state.systems.get_system<SystemType>();
}

template <typename SystemType>
const SystemType& Coordinator::get_system() const {
    return state.systems.get_system<SystemType>();
}

template <typename SystemType>
bool Coordinator::system_has_behavior(BehaviorId behavior) const {
    return state.systems.has_behavior<SystemType>(behavior);
}

template <typename SystemType>
std::size_t Coordinator::system_behavior_count() const {
    return state.systems.behavior_count<SystemType>();
}

template <typename Component>
ComponentTypeId Coordinator::component_type(ComponentType<Component> type) const {
    return state.components.component_type(type);
}

template <typename Component>
IntentId Coordinator::create_intent(BehaviorId owner, ComponentType<Component> type, ComponentSlotId slot, IntentLifetime lifetime, Component value, IntentPriority priority) {
    if (!state.behaviors.exists(owner))
        throw std::runtime_error("behavior id not found");

    ComponentTypeId typeId = component_type(type);

    if (!state.components.resolve_component(type, slot))
        throw std::runtime_error("component slot not found");

    if (!state.components.can_write(type, owner, slot))
        throw std::runtime_error("component write access denied");

    return state.intents.create(owner, ComponentType<Component>{typeId}, slot, lifetime, std::move(value), priority);
}

template <typename Component>
const ComponentIntent<Component>& Coordinator::typed_intent(ComponentType<Component> type, IntentId id) const {
    return state.intents.typed_intent(type, id);
}

template <typename Component>
void Coordinator::add_component(ComponentType<Component> type, std::string name, Component component) {
    state.components.add_component(type, std::move(name), std::move(component));
}

template <typename Component>
bool Coordinator::has_component_named(ComponentType<Component> type, const std::string& name) const {
    return state.components.has_component_named(type, name);
}

template <typename Component>
Component* Coordinator::get_component_named(ComponentType<Component> type, const std::string& name) {
    return state.components.get_component_named(type, name);
}

template <typename Component>
const Component* Coordinator::get_component_named(ComponentType<Component> type, const std::string& name) const {
    return state.components.get_component_named(type, name);
}

template <typename Component>
void Coordinator::remove_component(ComponentType<Component> type, const std::string& name) {
    ComponentTypeId typeId = state.components.component_type(type);
    ComponentSlotId slot = state.components.component_slot(type, name);
    std::vector<BehaviorId> affectedBehaviors = state.components.behaviors_with_access(type);
    std::vector<std::pair<BehaviorId, BehaviorAccessRevision>> accessRevisions;

    for (BehaviorId behavior : affectedBehaviors) {
        bool hasTargetAccess = state.components.can_read(type, behavior, slot) ||
            state.components.can_write(type, behavior, slot);

        if (!hasTargetAccess)
            continue;

        (void)state.behaviorAccessRevisions.at(behavior);
        accessRevisions.emplace_back(behavior, next_behavior_access_revision());
    }

    destroy_intents_for_target(typeId, slot);

    // ComponentRegistry clears storage access and name/slot indexes. Coordinator
    // first removes intents targeting the slot, then clears behavior signatures
    // for behaviors that no longer have any component of this type.
    state.components.remove_component(type, name);

    for (const auto& [behavior, revision] : accessRevisions)
        state.behaviorAccessRevisions.at(behavior) = revision;

    std::exception_ptr firstException;

    for (BehaviorId behavior : affectedBehaviors) {
        if (state.components.get_components(type, behavior).empty()) {
            state.behaviorSignatures[behavior].reset(typeId);

            try {
                update_system_memberships(behavior);
            } catch (...) {
                if (!firstException)
                    firstException = std::current_exception();
            }
        }
    }

    if (firstException)
        std::rethrow_exception(firstException);
}

template <typename Component>
void Coordinator::grant_component_access(ComponentType<Component> type, BehaviorId behavior, const std::string& name, ComponentAccessMode mode) {
    if (!state.behaviors.exists(behavior))
        throw std::runtime_error("behavior id not found");

    ComponentTypeId typeId = state.components.component_type(type);
    ComponentSlotId slot = state.components.component_slot(type, name);
    bool canRead = state.components.can_read(type, behavior, slot);
    bool canWrite = state.components.can_write(type, behavior, slot);
    bool accessChanges =
        (mode == ComponentAccessMode::Read && (!canRead || canWrite)) ||
        (mode == ComponentAccessMode::Write && (canRead || !canWrite)) ||
        (mode == ComponentAccessMode::ReadWrite && (!canRead || !canWrite)) ||
        (mode != ComponentAccessMode::Read &&
            mode != ComponentAccessMode::Write &&
            mode != ComponentAccessMode::ReadWrite);
    BehaviorAccessRevision nextRevision = 0;

    if (accessChanges) {
        (void)state.behaviorAccessRevisions.at(behavior);
        nextRevision = next_behavior_access_revision();
    }

    // Behavior existence is checked here, not in ComponentRegistry, so invalid
    // ownership is rejected at the world boundary.
    state.components.grant_access(type, behavior, name, mode);

    if (accessChanges)
        state.behaviorAccessRevisions.at(behavior) = nextRevision;

    if (mode == ComponentAccessMode::Read)
        destroy_intents_for_owner_target(behavior, typeId, slot);

    state.behaviorSignatures[behavior].set(typeId);
    update_system_memberships(behavior);
}

template <typename Component>
void Coordinator::revoke_component_access(ComponentType<Component> type, BehaviorId behavior, const std::string& name) {
    if (!state.behaviors.exists(behavior))
        throw std::runtime_error("behavior id not found");

    ComponentTypeId typeId = state.components.component_type(type);
    ComponentSlotId slot = state.components.component_slot(type, name);
    bool hadAccess = state.components.can_read(type, behavior, slot) ||
        state.components.can_write(type, behavior, slot);
    BehaviorAccessRevision nextRevision = 0;

    if (hadAccess) {
        (void)state.behaviorAccessRevisions.at(behavior);
        nextRevision = next_behavior_access_revision();
    }

    destroy_intents_for_owner_target(behavior, typeId, slot);

    state.components.revoke_access(type, behavior, name);

    if (hadAccess)
        state.behaviorAccessRevisions.at(behavior) = nextRevision;

    if (state.components.get_components(type, behavior).empty()) {
        state.behaviorSignatures[behavior].reset(typeId);
        update_system_memberships(behavior);
    }
}

template <typename Component>
std::map<std::string, ComponentSlotId> Coordinator::get_components(ComponentType<Component> type, BehaviorId behavior) const {
    if (!state.behaviors.exists(behavior))
        throw std::runtime_error("behavior id not found");

    return state.components.get_components(type, behavior);
}

template <typename Component>
const Component* Coordinator::read_component(ComponentType<Component> type, BehaviorId behavior, const std::string& name) const {
    if (!state.behaviors.exists(behavior))
        throw std::runtime_error("behavior id not found");

    ComponentSlotId slot = state.components.component_slot(type, name);

    if (!state.components.can_read(type, behavior, slot))
        throw std::runtime_error("component read access denied");

    // Pointers are only returned after both behavior existence and access mode
    // checks succeed.
    return state.components.resolve_component(type, slot);
}

template <typename Component>
Component* Coordinator::write_component(ComponentType<Component> type, BehaviorId behavior, const std::string& name) {
    if (!state.behaviors.exists(behavior))
        throw std::runtime_error("behavior id not found");

    ComponentSlotId slot = state.components.component_slot(type, name);

    if (!state.components.can_write(type, behavior, slot))
        throw std::runtime_error("component write access denied");

    // Write access is stricter than resolution: resolving by slot is internal,
    // but mutable access through Coordinator must pass permission checks.
    return state.components.resolve_component(type, slot);
}

template <typename Component>
Component* Coordinator::resolve_component(ComponentType<Component> type, ComponentSlotId slot) {
    return state.components.resolve_component(type, slot);
}

template <typename Component>
const Component* Coordinator::resolve_component(ComponentType<Component> type, ComponentSlotId slot) const {
    return state.components.resolve_component(type, slot);
}

template <typename Component>
bool Coordinator::can_read_component(ComponentType<Component> type, BehaviorId behavior, const std::string& name) const {
    return state.components.can_read(type, behavior, name);
}

template <typename Component>
bool Coordinator::can_write_component(ComponentType<Component> type, BehaviorId behavior, const std::string& name) const {
    return state.components.can_write(type, behavior, name);
}
