#pragma once

#include "liquid/detail/ComponentStorage.hpp"
#include "liquid/ComponentCodec.hpp"
#include "liquid/Ids.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace liquid::detail {

class ComponentRegistry {
private:
    WorldInstanceId worldId = 0;
    class IComponentCodec {
    public:
        virtual ~IComponentCodec() = default;
    };

    class IEffectCodec {
    public:
        virtual ~IEffectCodec() = default;
    };

    template <typename Component>
    class ComponentCodecModel : public IComponentCodec {
    public:
        liquid::ComponentCodec<Component> codec;

        explicit ComponentCodecModel(liquid::ComponentCodec<Component> configuredCodec)
            : codec(std::move(configuredCodec)) {
        }
    };

    template <typename Component>
    class EffectCodecModel : public IEffectCodec {
    public:
        liquid::EffectCodec<Component> codec;

        explicit EffectCodecModel(liquid::EffectCodec<Component> configuredCodec)
            : codec(std::move(configuredCodec)) {
        }
    };

    ComponentTypeId nextComponentType = 0;

    // Registration layer: stable names are converted to compact runtime IDs.
    // ComponentType<T> exposes the typed ID handle to outside code.
    std::map<TypeName, ComponentTypeId> componentTypes;
    std::vector<TypeName> typeNames;

    // Runtime lookup layer: ComponentTypeId is the source of truth after
    // registration. The storage map is type-erased so all typed storages can
    // live in one registry.
    std::map<ComponentTypeId, std::shared_ptr<IComponentStorage>> storages;
    // Per-type name indexes. ComponentStorage only knows slots; these maps give
    // systems the stable names they use to request component access.
    std::map<ComponentTypeId, std::map<ComponentName, ComponentSlotId>> componentNames;
    std::map<ComponentTypeId, std::map<ComponentSlotId, ComponentName>> slotNames;
    std::map<ComponentTypeId, liquid::ComponentSchema> schemas;
    std::map<ComponentTypeId, std::shared_ptr<IComponentCodec>> codecs;
    std::map<ComponentTypeId, std::shared_ptr<IEffectCodec>> effectCodecs;
    std::map<ComponentTypeId, std::vector<std::uint32_t>> slotGenerations;

    template <typename Component>
    std::shared_ptr<ComponentStorage<Component>> component_storage(ComponentTypeId type);

    template <typename Component>
    std::shared_ptr<const ComponentStorage<Component>> component_storage(ComponentTypeId type) const;

    template <typename Component>
    const liquid::ComponentCodec<Component>& component_codec(ComponentType<Component> type) const;

    const TypeName& type_name(ComponentTypeId type) const;
    ComponentSlotId named_slot(ComponentTypeId type, const ComponentName& name) const;
    ComponentSlotId make_slot_handle(ComponentTypeId type, Slot slot);
    Slot validate_slot(ComponentTypeId type, ComponentSlotId slot) const;
    bool advance_slot_generation(ComponentTypeId type, ComponentSlotId slot);

    template <typename Component>
    ComponentType<Component> register_component_storage(TypeName typeName);
    static ComponentSlotAccess::Mode storage_access_mode(ComponentAccessMode mode);

public:
    explicit ComponentRegistry(WorldInstanceId world = 0)
        : worldId(world) {
    }
    ComponentRegistry(const ComponentRegistry&) = delete;
    ComponentRegistry& operator=(const ComponentRegistry&) = delete;
    ComponentRegistry(ComponentRegistry&&) = delete;
    ComponentRegistry& operator=(ComponentRegistry&&) = delete;

#ifdef LIQUID_ENABLE_LEGACY_INTERNAL_COMPONENT_REGISTRATION
    template <typename Component>
    ComponentType<Component> register_component(TypeName typeName) {
        return register_component_storage<Component>(std::move(typeName));
    }

    template <typename Component>
    ComponentSlotId set_slot_generation_for_test(
        ComponentType<Component> type,
        const std::string& name,
        std::uint32_t generation) {
        const ComponentTypeId typeId = component_type(type);
        const ComponentSlotId current = named_slot(typeId, name);
        if (generation == 0)
            throw std::invalid_argument("test generation must be nonzero");
        slotGenerations.at(typeId).at(current.slot) = generation;
        const ComponentSlotId replacement{worldId, current.slot, generation};
        componentNames.at(typeId).at(name) = replacement;
        slotNames.at(typeId).erase(current);
        slotNames.at(typeId).emplace(replacement, name);
        return replacement;
    }
#endif

    template <typename Component>
    ComponentType<Component> register_component(
        TypeName schemaName,
        liquid::SchemaVersion schemaVersion,
        liquid::ComponentCodec<Component> codec
    );

    template <typename Component>
    const liquid::ComponentSchema& component_schema(ComponentType<Component> type) const;

    template <typename Component>
    liquid::Value encode_component(ComponentType<Component> type, const Component& component) const;

    template <typename Component>
    Component decode_component(ComponentType<Component> type, const liquid::Value& value) const;

    template <typename Component>
    bool has_component_codec(ComponentType<Component> type) const;

    template <typename Component>
    void register_effect_codec(
        ComponentType<Component> type,
        liquid::EffectCodec<Component> codec);

    template <typename Component>
    bool has_effect_codec(ComponentType<Component> type) const;

    template <typename Component>
    std::optional<liquid::ResolvedEffect> encode_effect(
        ComponentType<Component> type,
        const ComponentName& name,
        const Component& component) const;

    ComponentTypeId component_type(const TypeName& typeName) const;
    bool component_type_exists(ComponentTypeId type) const;
    bool component_matches(ComponentTypeId type, const ComponentName& name, ComponentSlotId slot) const;

    template <typename Component>
    ComponentTypeId component_type(ComponentType<Component> type) const;

    template <typename Component>
    void add_component(ComponentType<Component> type, std::string name, Component component);

    template <typename Component>
    bool has_component_named(ComponentType<Component> type, const std::string& name) const;

    template <typename Component>
    ComponentSlotId component_slot(ComponentType<Component> type, const std::string& name) const;

    template <typename Component>
    Component* get_component_named(ComponentType<Component> type, const std::string& name);

    template <typename Component>
    const Component* get_component_named(ComponentType<Component> type, const std::string& name) const;

    template <typename Component>
    void replace_component(ComponentType<Component> type, const std::string& name, Component component);

    template <typename Component>
    void remove_component(ComponentType<Component> type, const std::string& name);

    void remove_behavior(BehaviorId behavior);

    template <typename Component>
    void grant_access(ComponentType<Component> type, BehaviorId behavior, const std::string& name, ComponentAccessMode mode);

    template <typename Component>
    void revoke_access(ComponentType<Component> type, BehaviorId behavior, const std::string& name);

    template <typename Component>
    std::map<std::string, ComponentSlotId> get_components(ComponentType<Component> type, BehaviorId behavior) const;

    template <typename Component>
    std::vector<BehaviorId> behaviors_with_access(ComponentType<Component> type) const;

    template <typename Component>
    Component* resolve_component(ComponentType<Component> type, ComponentSlotId slot);

    template <typename Component>
    const Component* resolve_component(ComponentType<Component> type, ComponentSlotId slot) const;

    template <typename Component>
    bool can_read(ComponentType<Component> type, BehaviorId behavior, const std::string& name) const;

    template <typename Component>
    bool can_read(ComponentType<Component> type, BehaviorId behavior, ComponentSlotId slot) const;

    template <typename Component>
    bool can_write(ComponentType<Component> type, BehaviorId behavior, const std::string& name) const;

    template <typename Component>
    bool can_write(ComponentType<Component> type, BehaviorId behavior, ComponentSlotId slot) const;
};

template <typename Component>
std::shared_ptr<ComponentStorage<Component>> ComponentRegistry::component_storage(ComponentTypeId type) {
    auto found = storages.find(type);

    if (found == storages.end())
        throw std::runtime_error("component type storage not found");

    auto storage = std::dynamic_pointer_cast<ComponentStorage<Component>>(found->second);

    if (!storage)
        throw std::runtime_error("component storage type mismatch");

    return storage;
}

template <typename Component>
std::shared_ptr<const ComponentStorage<Component>> ComponentRegistry::component_storage(ComponentTypeId type) const {
    auto found = storages.find(type);

    if (found == storages.end())
        throw std::runtime_error("component type storage not found");

    auto storage = std::dynamic_pointer_cast<const ComponentStorage<Component>>(found->second);

    if (!storage)
        throw std::runtime_error("component storage type mismatch");

    return storage;
}

template <typename Component>
ComponentType<Component> ComponentRegistry::register_component_storage(TypeName typeName) {
    if (typeName.empty())
        throw std::runtime_error("component type name cannot be empty");

    if (componentTypes.contains(typeName))
        throw std::runtime_error("component type already registered");

    if (nextComponentType >= MaxComponentTypes)
        throw std::runtime_error("maximum component types reached");

    ComponentTypeId type = nextComponentType;
    auto storage = std::make_shared<ComponentStorage<Component>>();
    typeNames.reserve(typeNames.size() + 1);
    bool typeIndexed = false;
    bool storageIndexed = false;
    bool componentNamesCreated = false;
    bool slotNamesCreated = false;

    try {
        auto [typePosition, typeInserted] = componentTypes.emplace(typeName, type);
        (void)typePosition;

        if (!typeInserted)
            throw std::logic_error("component type already indexed");

        typeIndexed = true;

        auto [storagePosition, storageInserted] = storages.emplace(type, std::move(storage));
        (void)storagePosition;

        if (!storageInserted)
            throw std::logic_error("component storage already indexed");

        storageIndexed = true;

        auto [componentNamesPosition, componentNamesInserted] = componentNames.emplace(
            type,
            std::map<ComponentName, ComponentSlotId>{}
        );
        (void)componentNamesPosition;

        if (!componentNamesInserted)
            throw std::logic_error("component name index already exists");

        componentNamesCreated = true;

        auto [slotNamesPosition, slotNamesInserted] = slotNames.emplace(
            type,
            std::map<ComponentSlotId, ComponentName>{}
        );
        (void)slotNamesPosition;

        if (!slotNamesInserted)
            throw std::logic_error("component slot index already exists");

        slotNamesCreated = true;
        slotGenerations.emplace(type, std::vector<std::uint32_t>{});
        typeNames.push_back(typeName);
    } catch (...) {
        if (slotNamesCreated)
            slotNames.erase(type);

        slotGenerations.erase(type);

        if (componentNamesCreated)
            componentNames.erase(type);

        if (storageIndexed)
            storages.erase(type);

        if (typeIndexed)
            componentTypes.erase(typeName);

        throw;
    }

    schemas.emplace(type, liquid::ComponentSchema{typeName, 0});
    ++nextComponentType;

    return ComponentType<Component>{type, worldId, 1};
}

template <typename Component>
ComponentType<Component> ComponentRegistry::register_component(
    TypeName schemaName,
    liquid::SchemaVersion schemaVersion,
    liquid::ComponentCodec<Component> codec
) {
    if (schemaVersion == 0)
        throw std::invalid_argument("component schema version must be positive");

    if (!codec.encode || !codec.decode)
        throw std::invalid_argument("component codec must provide encode and decode");

    ComponentType<Component> type =
        register_component_storage<Component>(std::move(schemaName));
    schemas.at(type.id).version = schemaVersion;
    codecs.emplace(
        type.id,
        std::make_shared<ComponentCodecModel<Component>>(std::move(codec))
    );
    return type;
}

template <typename Component>
const liquid::ComponentCodec<Component>& ComponentRegistry::component_codec(
    ComponentType<Component> type
) const {
    component_type(type);
    auto found = codecs.find(type.id);

    if (found == codecs.end())
        throw std::runtime_error("component codec not registered");

    auto model = std::dynamic_pointer_cast<const ComponentCodecModel<Component>>(found->second);

    if (!model)
        throw std::runtime_error("component codec type mismatch");

    return model->codec;
}

template <typename Component>
const liquid::ComponentSchema& ComponentRegistry::component_schema(
    ComponentType<Component> type
) const {
    component_type(type);
    return schemas.at(type.id);
}

template <typename Component>
liquid::Value ComponentRegistry::encode_component(
    ComponentType<Component> type,
    const Component& component
) const {
    liquid::Value encoded = component_codec(type).encode(component);
    encoded.validate();
    return encoded;
}

template <typename Component>
Component ComponentRegistry::decode_component(
    ComponentType<Component> type,
    const liquid::Value& value
) const {
    value.validate();
    Component decoded = component_codec(type).decode(value);

    if (component_codec(type).encode(decoded) != value)
        throw std::invalid_argument("component codec is not canonical");

    return decoded;
}

template <typename Component>
bool ComponentRegistry::has_component_codec(ComponentType<Component> type) const {
    component_type(type);
    return codecs.contains(type.id);
}

template <typename Component>
void ComponentRegistry::register_effect_codec(
    ComponentType<Component> type,
    liquid::EffectCodec<Component> codec
) {
    component_type(type);
    if (!codec.encode)
        throw std::invalid_argument("effect codec must provide encode");
    if (effectCodecs.contains(type.id))
        throw std::invalid_argument("effect codec already registered");
    effectCodecs.emplace(
        type.id,
        std::make_shared<EffectCodecModel<Component>>(std::move(codec)));
}

template <typename Component>
bool ComponentRegistry::has_effect_codec(ComponentType<Component> type) const {
    component_type(type);
    return effectCodecs.contains(type.id);
}

template <typename Component>
std::optional<liquid::ResolvedEffect> ComponentRegistry::encode_effect(
    ComponentType<Component> type,
    const ComponentName& name,
    const Component& component
) const {
    component_type(type);
    const auto found = effectCodecs.find(type.id);
    if (found == effectCodecs.end())
        return std::nullopt;
    const auto model =
        std::dynamic_pointer_cast<const EffectCodecModel<Component>>(found->second);
    if (!model)
        throw std::runtime_error("effect codec type mismatch");
    auto effect = model->codec.encode(name, component);
    if (!effect)
        return std::nullopt;
    if (effect->adapterRoute != model->codec.adapterRoute)
        throw std::invalid_argument("effect codec returned an unstable adapter route");
    effect->desiredValue.validate();
    return effect;
}

template <typename Component>
ComponentTypeId ComponentRegistry::component_type(ComponentType<Component> type) const {
    if (type.world != 0 && type.world != worldId)
        throw std::runtime_error("component type belongs to another world");

    if (type.world != 0 && type.generation != 1)
        throw std::runtime_error("stale component type handle");

    component_storage<Component>(type.id);
    return type.id;
}

template <typename Component>
void ComponentRegistry::add_component(ComponentType<Component> type, std::string name, Component component) {
    if (name.empty())
        throw std::runtime_error("component name cannot be empty");

    ComponentTypeId typeId = component_type(type);

    if (componentNames[typeId].contains(name))
        throw std::runtime_error("component name already registered");

    auto storage = component_storage<Component>(typeId);
    const Slot rawSlot = storage->add(std::move(component));
    ComponentSlotId slot = make_slot_handle(typeId, rawSlot);
    bool nameIndexed = false;

    try {
        auto [namePosition, nameInserted] = componentNames.at(typeId).emplace(name, slot);
        (void)namePosition;

        if (!nameInserted)
            throw std::logic_error("component name already indexed");

        nameIndexed = true;

        auto [slotPosition, slotInserted] = slotNames.at(typeId).emplace(slot, name);
        (void)slotPosition;

        if (!slotInserted)
            throw std::logic_error("component slot already indexed");
    } catch (...) {
        if (nameIndexed)
            componentNames.at(typeId).erase(name);

        storage->remove(rawSlot);
        throw;
    }
}

template <typename Component>
bool ComponentRegistry::has_component_named(ComponentType<Component> type, const std::string& name) const {
    try {
        ComponentTypeId typeId = component_type(type);
        auto names = componentNames.find(typeId);

        if (names == componentNames.end())
            return false;

        auto found = names->second.find(name);

        if (found == names->second.end())
            return false;

        validate_slot(typeId, found->second);
        return true;
    } catch (const std::runtime_error&) {
        return false;
    }
}

template <typename Component>
ComponentSlotId ComponentRegistry::component_slot(ComponentType<Component> type, const std::string& name) const {
    return named_slot(component_type(type), name);
}

template <typename Component>
Component* ComponentRegistry::get_component_named(ComponentType<Component> type, const std::string& name) {
    ComponentTypeId typeId = component_type(type);
    ComponentSlotId slot = named_slot(typeId, name);

    return component_storage<Component>(typeId)->get(validate_slot(typeId, slot));
}

template <typename Component>
const Component* ComponentRegistry::get_component_named(ComponentType<Component> type, const std::string& name) const {
    ComponentTypeId typeId = component_type(type);
    ComponentSlotId slot = named_slot(typeId, name);

    return component_storage<Component>(typeId)->get(validate_slot(typeId, slot));
}

template <typename Component>
void ComponentRegistry::replace_component(
    ComponentType<Component> type,
    const std::string& name,
    Component component
) {
    ComponentTypeId typeId = component_type(type);
    ComponentSlotId slot = named_slot(typeId, name);
    component_storage<Component>(typeId)->replace(
        validate_slot(typeId, slot), std::move(component));
}

template <typename Component>
void ComponentRegistry::remove_component(ComponentType<Component> type, const std::string& name) {
    ComponentTypeId typeId = component_type(type);
    ComponentSlotId slot = named_slot(typeId, name);

    auto storage = component_storage<Component>(typeId);
    // Clear every behavior reference to the slot before recycling it, then
    // erase the name indexes so the slot can safely be reused for a new name.
    storage->removeAccessesTo(slot);
    const Slot rawSlot = validate_slot(typeId, slot);
    const bool recycle = advance_slot_generation(typeId, slot);
    storage->remove(rawSlot, recycle);

    componentNames[typeId].erase(name);
    slotNames[typeId].erase(slot);
}

template <typename Component>
void ComponentRegistry::grant_access(ComponentType<Component> type, BehaviorId behavior, const std::string& name, ComponentAccessMode mode) {
    ComponentTypeId typeId = component_type(type);
    ComponentSlotId slot = named_slot(typeId, name);
    auto storage = component_storage<Component>(typeId);
    ComponentSlotAccess::Mode storageMode = storage_access_mode(mode);

    // Replace any previous access for this behavior/slot pair so a grant is an
    // update, not a duplicate access record.
    storage->removeAccess(behavior, slot);
    storage->addAccess(behavior, storageMode, slot);
}

template <typename Component>
void ComponentRegistry::revoke_access(ComponentType<Component> type, BehaviorId behavior, const std::string& name) {
    ComponentTypeId typeId = component_type(type);
    ComponentSlotId slot = named_slot(typeId, name);

    component_storage<Component>(typeId)->removeAccess(behavior, slot);
}

template <typename Component>
std::map<std::string, ComponentSlotId> ComponentRegistry::get_components(ComponentType<Component> type, BehaviorId behavior) const {
    std::map<std::string, ComponentSlotId> available;
    ComponentTypeId typeId = component_type(type);
    auto storage = component_storage<Component>(typeId);
    const auto& allAccesses = storage->allAccesses();
    auto behaviorAccesses = allAccesses.find(behavior);

    if (behaviorAccesses == allAccesses.end())
        return available;

    auto names = slotNames.find(typeId);

    if (names == slotNames.end())
        return available;

    // This is the system-facing view: only live, named slots that the behavior
    // can access are returned.
    for (const ComponentSlotAccess& access : behaviorAccesses->second) {
        if (!storage->has(access.slot))
            continue;

        auto name = names->second.find(access.slot);

        if (name != names->second.end())
            available[name->second] = access.slot;
    }

    return available;
}

template <typename Component>
std::vector<BehaviorId> ComponentRegistry::behaviors_with_access(ComponentType<Component> type) const {
    std::vector<BehaviorId> behaviors;
    auto storage = component_storage<Component>(component_type(type));

    for (const auto& [behavior, accesses] : storage->allAccesses()) {
        if (!accesses.empty())
            behaviors.push_back(behavior);
    }

    std::sort(behaviors.begin(), behaviors.end());
    return behaviors;
}

template <typename Component>
Component* ComponentRegistry::resolve_component(ComponentType<Component> type, ComponentSlotId slot) {
    const ComponentTypeId typeId = component_type(type);
    return component_storage<Component>(typeId)->get(validate_slot(typeId, slot));
}

template <typename Component>
const Component* ComponentRegistry::resolve_component(ComponentType<Component> type, ComponentSlotId slot) const {
    const ComponentTypeId typeId = component_type(type);
    return component_storage<Component>(typeId)->get(validate_slot(typeId, slot));
}

template <typename Component>
bool ComponentRegistry::can_read(ComponentType<Component> type, BehaviorId behavior, const std::string& name) const {
    try {
        ComponentTypeId typeId = component_type(type);
        ComponentSlotId slot = named_slot(typeId, name);
        return can_read(type, behavior, slot);
    } catch (const std::runtime_error&) {
        return false;
    }
}

template <typename Component>
bool ComponentRegistry::can_read(ComponentType<Component> type, BehaviorId behavior, ComponentSlotId slot) const {
    try {
        ComponentTypeId typeId = component_type(type);
        auto storage = component_storage<Component>(typeId);

        validate_slot(typeId, slot);

        const auto& allAccesses = storage->allAccesses();
        auto accesses = allAccesses.find(behavior);

        if (accesses == allAccesses.end())
            return false;

        for (const ComponentSlotAccess& access : accesses->second) {
            if (access.slot == slot && (access.mode == ComponentSlotAccess::r || access.mode == ComponentSlotAccess::rw))
                return true;
        }
    } catch (const std::runtime_error&) {
        return false;
    }

    return false;
}

template <typename Component>
bool ComponentRegistry::can_write(ComponentType<Component> type, BehaviorId behavior, ComponentSlotId slot) const {
    try {
        ComponentTypeId typeId = component_type(type);
        auto storage = component_storage<Component>(typeId);

        validate_slot(typeId, slot);

        const auto& allAccesses = storage->allAccesses();
        auto accesses = allAccesses.find(behavior);

        if (accesses == allAccesses.end())
            return false;

        for (const ComponentSlotAccess& access : accesses->second) {
            if (access.slot == slot && (access.mode == ComponentSlotAccess::w || access.mode == ComponentSlotAccess::rw))
                return true;
        }
    } catch (const std::runtime_error&) {
        return false;
    }

    return false;
}

template <typename Component>
bool ComponentRegistry::can_write(ComponentType<Component> type, BehaviorId behavior, const std::string& name) const {
    try {
        ComponentTypeId typeId = component_type(type);
        ComponentSlotId slot = named_slot(typeId, name);
        return can_write(type, behavior, slot);
    } catch (const std::runtime_error&) {
        return false;
    }
}

}
