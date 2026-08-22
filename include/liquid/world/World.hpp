#pragma once

#include "liquid/ComponentCodec.hpp"
#include "liquid/Ids.hpp"
#include "liquid/Intent.hpp"
#include "liquid/IntentLifetime.hpp"
#include "liquid/System.hpp"
#include "liquid/detail/Coordinator.hpp"
#include "liquid/detail/WorldState.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <functional>
#include <memory>
#include <thread>

namespace liquid {

class Runtime;
namespace scripting { class LuaBehaviorRunner; }

struct ComponentMutation {
    ComponentTarget target;
    ComponentName name;
    Value before;
    Value after;
    bool removed = false;
};

struct TopologyMutation {
    std::string key;
    Value value;
    bool removed = false;
};

struct ScriptExecutionEvidence {
    BehaviorId owner;
    IntentTime now = 0;
    std::string source;
    std::string sourceHash;
    bool sourceIncluded = true;
    std::uint32_t status = 0;
    std::string diagnostic;
    std::size_t createdIntentCount = 0;
};

// World and Runtime are single-thread-confined. A World owns all handles and
// borrowed component views for one runtime session; callers must synchronize
// externally and must not expose this trusted host boundary to scripts.
class World {
private:
    friend class Runtime;
    friend class scripting::LuaBehaviorRunner;

    detail::WorldState state;
    detail::Coordinator coordinator;
    bool systemsRunning = false;
    std::vector<liquid::ComponentMutation> componentMutations;
    std::vector<liquid::TopologyMutation> topologyMutations;
    std::vector<liquid::ScriptExecutionEvidence> scriptExecutions;
    bool scriptEvidenceEnabled = false;
    bool intentTransactionActive = false;
    std::thread::id ownerThread;

    void ensure_structural_mutation_allowed() const;
    void ensure_owner_thread() const;
    void record_topology(std::string key, Value value, bool removed = false);
    void record_component_mutation(ComponentMutation mutation);
    void enable_script_evidence();
    void record_script_execution(ScriptExecutionEvidence evidence);
    std::unique_ptr<detail::IntentRegistry::Transaction> begin_intent_transaction(
        std::size_t cancellationCount);
    void cancel_intent_transaction(
        detail::IntentRegistry::Transaction& transaction,
        IntentId id
    );
    void commit_intent_transaction(
        detail::IntentRegistry::Transaction& transaction);
    void rollback_intent_transaction(
        detail::IntentRegistry::Transaction& transaction) noexcept;
    template <typename Component>
    IntentId create_intent_transaction(
        detail::IntentRegistry::Transaction& transaction,
        BehaviorId owner,
        ComponentType<Component> type,
        ComponentSlotId slot,
        IntentLifetime lifetime,
        Component value,
        IntentPriority priority,
        IntentName name
    );
    bool resolution_request_is_current(
        ComponentTypeId type,
        const std::map<ComponentName, ComponentSlotId>& components
    ) const;
    std::size_t destroy_expired_intents(IntentTime now);
    const std::vector<detail::IntentLifecycleRecord>& intent_lifecycle_records() const;
    void clear_intent_lifecycle_records();
    std::map<ComponentName, IntentId> resolve_intents(
        ComponentTypeId type,
        const std::map<ComponentName, ComponentSlotId>& components,
        IntentTime now
    );
    std::map<ComponentTypeId,
        std::map<ComponentName, ComponentSlotId>> resolution_targets() const;
    const AdapterRoute& effect_route(ComponentTypeId type) const;
    std::optional<ResolvedEffect> encode_effect(
        ComponentTarget target,
        const ComponentName& name,
        const Value& desired) const;
    Value component_value(ComponentTarget target) const;
    Value decode_observed(
        ComponentTarget target, const Value& observed) const;
    Value replace_component_value(
        ComponentTarget target,
        const ComponentName& name,
        const Value& replacement);

    template <typename Component>
    ComponentTarget component_target(
        ComponentType<Component> type,
        const ComponentName& name) const;
    std::size_t run_systems(
        FrameNumber frame,
        IntentTime now,
        SystemPhase phase,
        std::size_t* completedSystems = nullptr,
        std::string* failingSystem = nullptr
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
    std::vector<IntentId> intents_owned_by(BehaviorId owner) const;
    std::optional<IntentId> intent_named(
        BehaviorId owner, const IntentName& name) const;
    std::vector<IntentId> intents_for(ComponentTypeId type, ComponentSlotId slot) const;
    const IntentTargetIndex& intent_target_index() const;
    std::size_t intent_count(BehaviorId owner);

    template <typename SystemType, typename... Args>
    void register_system(Signature signature, Args&&... args);

    template <typename SystemType, typename... Args>
    void register_system(Signature signature, SystemPhase phase, Args&&... args);

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

#ifdef LIQUID_ENABLE_LEGACY_INTERNAL_COMPONENT_REGISTRATION
    template <typename Component>
    ComponentType<Component> register_component(TypeName typeName);
#endif

    template <typename Component>
    ComponentType<Component> register_component(
        TypeName schemaName,
        liquid::SchemaVersion schemaVersion,
        liquid::ComponentCodec<Component> codec
    );

    template <typename Component>
    void register_effect_codec(
        ComponentType<Component> type,
        liquid::EffectCodec<Component> codec);

    template <typename Component>
    std::optional<liquid::ResolvedEffect> resolve_effect(
        ComponentType<Component> type,
        BehaviorId behavior,
        const ComponentName& name) const;

    template <typename Component>
    const liquid::ComponentSchema& component_schema(ComponentType<Component> type) const;

    template <typename Component>
    Component decode_component(ComponentType<Component> type, const liquid::Value& value) const;

    ComponentTypeId component_type(const TypeName& typeName) const;

    template <typename Component>
    ComponentTypeId component_type(ComponentType<Component> type) const;

    template <typename Component>
    void add_component(ComponentType<Component> type, std::string name, Component component);

    template <typename Component>
    bool has_component_named(ComponentType<Component> type, const std::string& name) const;

    template <typename Component>
    const Component* get_component_named(ComponentType<Component> type, const std::string& name);

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

    template <typename Component, typename Update>
    void update_component(
        ComponentType<Component> type,
        BehaviorId behavior,
        const std::string& name,
        Update&& update
    );

    template <typename Component>
    void replace_component(
        ComponentType<Component> type,
        BehaviorId behavior,
        const std::string& name,
        Component replacement
    );

    template <typename Component>
    const Component* resolve_component(ComponentType<Component> type, ComponentSlotId slot) const;

    template <typename Component>
    IntentId create_intent(
        BehaviorId owner,
        ComponentType<Component> type,
        ComponentSlotId slot,
        IntentLifetime lifetime,
        Component value,
        IntentPriority priority = IntentPriority::Medium,
        IntentName name = {}
    );

    template <typename Component>
    const ComponentIntent<Component>& typed_intent(ComponentType<Component> type, IntentId id) const;

    template <typename Component>
    bool can_read_component(ComponentType<Component> type, BehaviorId behavior, const std::string& name) const;

    template <typename Component>
    bool can_write_component(ComponentType<Component> type, BehaviorId behavior, const std::string& name) const;

    Signature behavior_signature(BehaviorId behavior) const;
    const std::vector<liquid::ComponentMutation>& component_mutations() const;
    void clear_component_mutations();
    const std::vector<TopologyMutation>& topology_mutations() const;
    void clear_topology_mutations();
    const std::vector<ScriptExecutionEvidence>& script_execution_evidence() const;
    void clear_script_execution_evidence();
};

template <typename SystemType, typename... Args>
void World::register_system(Signature signature, Args&&... args) {
    ensure_structural_mutation_allowed();
    coordinator.register_system<SystemType>(signature, std::forward<Args>(args)...);
    Value::Object value;
    value.emplace("name", Value{std::string{SystemType::stableName}});
    value.emplace("version", Value{static_cast<std::uint64_t>(SystemType::version)});
    value.emplace("signature", Value{signature.to_string()});
    value.emplace("phase", Value{"decision"});
    record_topology(
        "system:" + std::string{SystemType::stableName},
        Value{std::move(value)});
}

template <typename SystemType, typename... Args>
void World::register_system(
    Signature signature,
    SystemPhase phase,
    Args&&... args
) {
    ensure_structural_mutation_allowed();
    coordinator.register_system<SystemType>(
        signature, phase, std::forward<Args>(args)...);
    Value::Object value;
    value.emplace("name", Value{std::string{SystemType::stableName}});
    value.emplace("version", Value{static_cast<std::uint64_t>(SystemType::version)});
    value.emplace("signature", Value{signature.to_string()});
    value.emplace("phase", Value{
        phase == SystemPhase::Input ? "input" :
        phase == SystemPhase::Behavior ? "behavior" : "decision"});
    record_topology(
        "system:" + std::string{SystemType::stableName},
        Value{std::move(value)});
}

template <typename SystemType>
void World::destroy_system() {
    ensure_structural_mutation_allowed();
    coordinator.destroy_system<SystemType>();
    record_topology(
        "system:" + std::string{SystemType::stableName}, Value{}, true);
}

template <typename Component>
ComponentTarget World::component_target(
    ComponentType<Component> type,
    const ComponentName& name
) const {
    ensure_owner_thread();
    return ComponentTarget{
        type.id, coordinator.component_slot(type, name)};
}

template <typename SystemType>
bool World::system_exists() const {
    ensure_owner_thread();
    return coordinator.system_exists<SystemType>();
}

template <typename SystemType>
void World::set_system_signature(Signature signature) {
    ensure_structural_mutation_allowed();
    coordinator.set_system_signature<SystemType>(signature);
    Value::Object value;
    value.emplace("name", Value{std::string{SystemType::stableName}});
    value.emplace("version", Value{static_cast<std::uint64_t>(SystemType::version)});
    value.emplace("signature", Value{signature.to_string()});
    record_topology(
        "system:" + std::string{SystemType::stableName},
        Value{std::move(value)});
}

template <typename SystemType>
Signature World::system_signature() const {
    ensure_owner_thread();
    return coordinator.system_signature<SystemType>();
}

template <typename SystemType>
SystemType& World::get_system() {
    ensure_owner_thread();
    return coordinator.get_system<SystemType>();
}

template <typename SystemType>
const SystemType& World::get_system() const {
    ensure_owner_thread();
    return coordinator.get_system<SystemType>();
}

template <typename SystemType>
bool World::system_has_behavior(BehaviorId behavior) const {
    ensure_owner_thread();
    return coordinator.system_has_behavior<SystemType>(behavior);
}

template <typename SystemType>
std::size_t World::system_behavior_count() const {
    ensure_owner_thread();
    return coordinator.system_behavior_count<SystemType>();
}

#ifdef LIQUID_ENABLE_LEGACY_INTERNAL_COMPONENT_REGISTRATION
template <typename Component>
ComponentType<Component> World::register_component(TypeName typeName) {
    ensure_structural_mutation_allowed();
    const TypeName stableName = typeName;
    auto type = coordinator.register_component<Component>(std::move(typeName));
    Value::Object value;
    value.emplace("name", Value{stableName});
    value.emplace("version", Value{std::uint64_t{0}});
    value.emplace("type", Value{static_cast<std::uint64_t>(type.id)});
    value.emplace("world", Value{type.world});
    record_topology("component_type:" + stableName, Value{std::move(value)});
    return type;
}
#endif

template <typename Component>
ComponentType<Component> World::register_component(
    TypeName schemaName,
    liquid::SchemaVersion schemaVersion,
    liquid::ComponentCodec<Component> codec
) {
    ensure_structural_mutation_allowed();
    const TypeName stableName = schemaName;
    auto type = coordinator.register_component<Component>(
        std::move(schemaName),
        schemaVersion,
        std::move(codec)
    );
    Value::Object value;
    value.emplace("name", Value{stableName});
    value.emplace("version", Value{static_cast<std::uint64_t>(schemaVersion)});
    value.emplace("type", Value{static_cast<std::uint64_t>(type.id)});
    value.emplace("world", Value{type.world});
    record_topology("component_type:" + stableName, Value{std::move(value)});
    return type;
}

template <typename Component>
void World::register_effect_codec(
    ComponentType<Component> type,
    liquid::EffectCodec<Component> codec
) {
    ensure_structural_mutation_allowed();
    coordinator.register_effect_codec(type, std::move(codec));
}

template <typename Component>
std::optional<liquid::ResolvedEffect> World::resolve_effect(
    ComponentType<Component> type,
    BehaviorId behavior,
    const ComponentName& name
) const {
    ensure_owner_thread();
    const Component* component = read_component(type, behavior, name);
    if (!component)
        return std::nullopt;
    return coordinator.encode_effect(type, name, *component);
}

template <typename Component>
const liquid::ComponentSchema& World::component_schema(ComponentType<Component> type) const {
    ensure_owner_thread();
    return coordinator.component_schema(type);
}

template <typename Component>
Component World::decode_component(
    ComponentType<Component> type,
    const liquid::Value& value
) const {
    ensure_owner_thread();
    return coordinator.decode_component(type, value);
}

template <typename Component>
ComponentTypeId World::component_type(ComponentType<Component> type) const {
    ensure_owner_thread();
    return coordinator.component_type(type);
}

template <typename Component>
void World::add_component(ComponentType<Component> type, std::string name, Component component) {
    ensure_structural_mutation_allowed();
    const std::string stableName = name;
    std::optional<Value> encoded;
    if (coordinator.has_component_codec(type))
        encoded = coordinator.encode_component(type, component);
    coordinator.add_component(type, std::move(name), std::move(component));
    const ComponentSlotId slot = state.components.component_slot(type, stableName);
    if (encoded) {
        record_component_mutation(ComponentMutation{
            ComponentTarget{type.id, slot}, stableName, Value{},
            std::move(*encoded), false});
    }
    Value::Object value;
    value.emplace("type", Value{static_cast<std::uint64_t>(type.id)});
    value.emplace("name", Value{stableName});
    value.emplace("slot_world", Value{slot.world});
    value.emplace("slot", Value{static_cast<std::uint64_t>(slot.slot)});
    value.emplace("generation", Value{static_cast<std::uint64_t>(slot.generation)});
    record_topology(
        "component:" + std::to_string(type.id) + ":" + stableName,
        Value{std::move(value)});
}

template <typename Component>
bool World::has_component_named(ComponentType<Component> type, const std::string& name) const {
    ensure_owner_thread();
    return coordinator.has_component_named(type, name);
}

template <typename Component>
const Component* World::get_component_named(ComponentType<Component> type, const std::string& name) {
    ensure_owner_thread();
    return static_cast<const detail::Coordinator&>(coordinator).get_component_named(type, name);
}

template <typename Component>
const Component* World::get_component_named(ComponentType<Component> type, const std::string& name) const {
    ensure_owner_thread();
    return coordinator.get_component_named(type, name);
}

template <typename Component>
void World::remove_component(ComponentType<Component> type, const std::string& name) {
    ensure_structural_mutation_allowed();
    const ComponentSlotId slot = state.components.component_slot(type, name);
    std::optional<Value> encoded;
    if (coordinator.has_component_codec(type)) {
        const Component* component = coordinator.get_component_named(type, name);
        if (!component)
            throw std::logic_error("component disappeared before removal");
        encoded = coordinator.encode_component(type, *component);
    }
    coordinator.remove_component(type, name);
    if (encoded) {
        record_component_mutation(ComponentMutation{
            ComponentTarget{type.id, slot}, name, std::move(*encoded),
            Value{}, true});
    }
    record_topology(
        "component:" + std::to_string(type.id) + ":" + name,
        Value{}, true);
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
    Value::Object value;
    value.emplace("behavior_world", Value{behavior.world});
    value.emplace("behavior_slot", Value{static_cast<std::uint64_t>(behavior.slot)});
    value.emplace("behavior_generation", Value{static_cast<std::uint64_t>(behavior.generation)});
    value.emplace("type", Value{static_cast<std::uint64_t>(type.id)});
    value.emplace("component", Value{name});
    value.emplace("mode", Value{static_cast<std::uint64_t>(mode)});
    record_topology(
        "access:" + std::to_string(behavior.world) + ":" +
            std::to_string(behavior.slot) + ":" +
            std::to_string(behavior.generation) + ":" +
            std::to_string(type.id) + ":" + name,
        Value{std::move(value)});
}

template <typename Component>
void World::revoke_component_access(ComponentType<Component> type, BehaviorId behavior, const std::string& name) {
    ensure_structural_mutation_allowed();
    coordinator.revoke_component_access(type, behavior, name);
    record_topology(
        "access:" + std::to_string(behavior.world) + ":" +
            std::to_string(behavior.slot) + ":" +
            std::to_string(behavior.generation) + ":" +
            std::to_string(type.id) + ":" + name,
        Value{}, true);
}

template <typename Component>
std::map<std::string, ComponentSlotId> World::get_components(ComponentType<Component> type, BehaviorId behavior) const {
    ensure_owner_thread();
    return coordinator.get_components(type, behavior);
}

template <typename Component>
const Component* World::read_component(ComponentType<Component> type, BehaviorId behavior, const std::string& name) const {
    ensure_owner_thread();
    return coordinator.read_component(type, behavior, name);
}

template <typename Component>
const Component* World::resolve_component(ComponentType<Component> type, ComponentSlotId slot) const {
    ensure_owner_thread();
    return coordinator.resolve_component(type, slot);
}

template <typename Component, typename Update>
void World::update_component(
    ComponentType<Component> type,
    BehaviorId behavior,
    const std::string& name,
    Update&& update
) {
    const Component* current = read_component(type, behavior, name);

    if (!current)
        throw std::runtime_error("component not found");

    liquid::Value currentValue = coordinator.encode_component(type, *current);
    Component staged = coordinator.decode_component(type, currentValue);
    std::invoke(std::forward<Update>(update), staged);
    replace_component(type, behavior, name, std::move(staged));
}

template <typename Component>
void World::replace_component(
    ComponentType<Component> type,
    BehaviorId behavior,
    const std::string& name,
    Component replacement
) {
    ensure_owner_thread();
    const Component* current = read_component(type, behavior, name);

    if (!current)
        throw std::runtime_error("component not found");

    liquid::Value before = coordinator.encode_component(type, *current);
    liquid::Value after = coordinator.encode_component(type, replacement);
    Component validated = coordinator.decode_component(type, after);
    ComponentSlotId slot = get_components(type, behavior).at(name);
    coordinator.replace_component(type, behavior, name, std::move(validated));
    record_component_mutation(
        {{type.id, slot}, name, std::move(before), std::move(after)});
}

template <typename Component>
IntentId World::create_intent(
    BehaviorId owner,
    ComponentType<Component> type,
    ComponentSlotId slot,
    IntentLifetime lifetime,
    Component value,
    IntentPriority priority,
    IntentName name
) {
    return coordinator.create_intent(
        owner, type, slot, lifetime, std::move(value), priority, std::move(name));
}

template <typename Component>
IntentId World::create_intent_transaction(
    detail::IntentRegistry::Transaction& transaction,
    BehaviorId owner,
    ComponentType<Component> type,
    ComponentSlotId slot,
    IntentLifetime lifetime,
    Component value,
    IntentPriority priority,
    IntentName name
) {
    ensure_owner_thread();
    return coordinator.create_intent(
        transaction, owner, type, slot, lifetime, std::move(value), priority,
        std::move(name));
}

template <typename Component>
const ComponentIntent<Component>& World::typed_intent(ComponentType<Component> type, IntentId id) const {
    ensure_owner_thread();
    return coordinator.typed_intent(type, id);
}

template <typename Component>
bool World::can_read_component(ComponentType<Component> type, BehaviorId behavior, const std::string& name) const {
    ensure_owner_thread();
    return coordinator.can_read_component(type, behavior, name);
}

template <typename Component>
bool World::can_write_component(ComponentType<Component> type, BehaviorId behavior, const std::string& name) const {
    ensure_owner_thread();
    return coordinator.can_write_component(type, behavior, name);
}

}
