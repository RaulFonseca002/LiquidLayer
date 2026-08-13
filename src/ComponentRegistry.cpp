#include "liquid/detail/ComponentRegistry.hpp"

#include <limits>
#include <stdexcept>

namespace liquid::detail {

ComponentTypeId ComponentRegistry::component_type(const TypeName& typeName) const {
    auto found = componentTypes.find(typeName);

    if (found == componentTypes.end())
        throw std::runtime_error("component type not registered");

    return found->second;
}

bool ComponentRegistry::component_type_exists(ComponentTypeId type) const {
    return type < typeNames.size() && storages.contains(type);
}

bool ComponentRegistry::component_matches(
    ComponentTypeId type,
    const ComponentName& name,
    ComponentSlotId slot
) const {
    try {
        validate_slot(type, slot);
    } catch (const std::runtime_error&) {
        return false;
    }

    auto typeComponents = componentNames.find(type);

    if (typeComponents == componentNames.end())
        return false;

    auto found = typeComponents->second.find(name);
    return found != typeComponents->second.end() && found->second == slot;
}

void ComponentRegistry::remove_behavior(BehaviorId behavior) {
    for (auto& [type, storage] : storages) {
        (void)type;
        storage->removeAccessesOf(behavior);
    }
}

const TypeName& ComponentRegistry::type_name(ComponentTypeId type) const {
    if (type >= typeNames.size())
        throw std::runtime_error("component type not registered");

    return typeNames[type];
}

const AdapterRoute& ComponentRegistry::effect_route(ComponentTypeId type) const {
    if (!component_type_exists(type))
        throw std::runtime_error("component type not registered");
    const auto found = effectCodecs.find(type);
    if (found == effectCodecs.end())
        throw std::runtime_error("effect codec not registered");
    return found->second->route();
}

std::optional<liquid::ResolvedEffect> ComponentRegistry::encode_effect(
    ComponentTypeId type,
    const ComponentName& name,
    const liquid::Value& value
) const {
    if (!component_type_exists(type))
        throw std::runtime_error("component type not registered");
    const auto found = effectCodecs.find(type);
    if (found == effectCodecs.end())
        return std::nullopt;
    return found->second->encode(name, value);
}

liquid::Value ComponentRegistry::decode_observed(
    ComponentTypeId type,
    const liquid::Value& value
) const {
    if (!component_type_exists(type))
        throw std::runtime_error("component type not registered");
    const auto found = effectCodecs.find(type);
    if (found == effectCodecs.end())
        throw std::runtime_error("effect codec not registered");
    return found->second->decode_observed(value);
}

liquid::Value ComponentRegistry::encode_component(
    ComponentTypeId type,
    ComponentSlotId slot
) const {
    const Slot rawSlot = validate_slot(type, slot);
    const auto codec = codecs.find(type);
    const auto storage = storages.find(type);
    if (codec == codecs.end() || storage == storages.end())
        throw std::runtime_error("component codec not registered");
    return codec->second->encode_slot(*storage->second, rawSlot);
}

liquid::Value ComponentRegistry::replace_component(
    ComponentTypeId type,
    ComponentSlotId slot,
    const liquid::Value& value
) {
    const Slot rawSlot = validate_slot(type, slot);
    const auto codec = codecs.find(type);
    const auto storage = storages.find(type);
    if (codec == codecs.end() || storage == storages.end())
        throw std::runtime_error("component codec not registered");
    return codec->second->replace_slot(*storage->second, rawSlot, value);
}

const ComponentName& ComponentRegistry::component_name(
    ComponentTypeId type,
    ComponentSlotId slot
) const {
    validate_slot(type, slot);
    const auto types = slotNames.find(type);
    if (types == slotNames.end())
        throw std::runtime_error("component type not registered");
    const auto found = types->second.find(slot);
    if (found == types->second.end())
        throw std::runtime_error("component slot has no stable name");
    return found->second;
}

ComponentSlotId ComponentRegistry::named_slot(ComponentTypeId type, const ComponentName& name) const {
    auto typeComponents = componentNames.find(type);

    if (typeComponents == componentNames.end())
        throw std::runtime_error("component type not registered");

    auto found = typeComponents->second.find(name);

    if (found == typeComponents->second.end())
        throw std::runtime_error("component name not registered");

    return found->second;
}

ComponentSlotId ComponentRegistry::make_slot_handle(ComponentTypeId type, Slot slot) {
    auto generations = slotGenerations.find(type);
    if (generations == slotGenerations.end())
        throw std::runtime_error("component type generations not registered");

    if (generations->second.size() <= slot)
        generations->second.resize(static_cast<std::size_t>(slot) + 1, 1);

    const std::uint32_t generation = generations->second[slot];
    if (generation == 0)
        throw std::overflow_error("component slot generation exhausted");
    return ComponentSlotId{worldId, slot, generation};
}

Slot ComponentRegistry::validate_slot(
    ComponentTypeId type,
    ComponentSlotId slot
) const {
    if (!slot || slot.world != worldId)
        throw std::runtime_error("component slot belongs to another world");

    const auto generations = slotGenerations.find(type);
    if (generations == slotGenerations.end() ||
        slot.slot >= generations->second.size() ||
        generations->second[slot.slot] != slot.generation) {
        throw std::runtime_error("stale component slot handle");
    }
    return slot.slot;
}

bool ComponentRegistry::advance_slot_generation(
    ComponentTypeId type,
    ComponentSlotId slot
) {
    const Slot rawSlot = validate_slot(type, slot);
    auto& generation = slotGenerations.at(type).at(rawSlot);
    if (generation == std::numeric_limits<std::uint32_t>::max()) {
        generation = 0;
        return false;
    }
    ++generation;
    return true;
}

ComponentSlotAccess::Mode ComponentRegistry::storage_access_mode(ComponentAccessMode mode) {
    switch (mode) {
    case ComponentAccessMode::Read:
        return ComponentSlotAccess::r;
    case ComponentAccessMode::Write:
        return ComponentSlotAccess::w;
    case ComponentAccessMode::ReadWrite:
        return ComponentSlotAccess::rw;
    }

    throw std::runtime_error("unknown component access mode");
}

}
