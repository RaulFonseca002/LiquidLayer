#pragma once

#include "liquid/ComponentStorage.hpp"
#include "liquid/Ids.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

class ComponentRegistry {
private:
    ComponentTypeId nextComponentType = 0;

    // Registration layer: stable names are converted to compact runtime IDs.
    // ComponentType<T> exposes the typed ID handle to outside code.
    std::map<TypeName, ComponentTypeId> componentTypes;
    std::vector<TypeName> typeNames;

    // Runtime lookup layer: ComponentTypeId is the source of truth after
    // registration. The storage map is type-erased so all typed storages can
    // live in one registry.
    std::map<ComponentTypeId, std::shared_ptr<liquid::IComponentStorage>> storages;
    // Per-type name indexes. ComponentStorage only knows slots; these maps give
    // systems the stable names they use to request component access.
    std::map<ComponentTypeId, std::map<ComponentName, ComponentSlotId>> componentNames;
    std::map<ComponentTypeId, std::map<ComponentSlotId, ComponentName>> slotNames;

    template <typename Component>
    std::shared_ptr<liquid::ComponentStorage<Component>> component_storage(ComponentTypeId type);

    template <typename Component>
    std::shared_ptr<const liquid::ComponentStorage<Component>> component_storage(ComponentTypeId type) const;

    const TypeName& type_name(ComponentTypeId type) const;
    ComponentSlotId named_slot(ComponentTypeId type, const ComponentName& name) const;
    static liquid::ComponentSlotAccess::Mode storage_access_mode(ComponentAccessMode mode);

public:
    ComponentRegistry() = default;
    ComponentRegistry(const ComponentRegistry&) = delete;
    ComponentRegistry& operator=(const ComponentRegistry&) = delete;
    ComponentRegistry(ComponentRegistry&&) = delete;
    ComponentRegistry& operator=(ComponentRegistry&&) = delete;

    template <typename Component>
    ComponentType<Component> register_component(TypeName typeName);

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
std::shared_ptr<liquid::ComponentStorage<Component>> ComponentRegistry::component_storage(ComponentTypeId type) {
    auto found = storages.find(type);

    if (found == storages.end())
        throw std::runtime_error("component type storage not found");

    auto storage = std::dynamic_pointer_cast<liquid::ComponentStorage<Component>>(found->second);

    if (!storage)
        throw std::runtime_error("component storage type mismatch");

    return storage;
}

template <typename Component>
std::shared_ptr<const liquid::ComponentStorage<Component>> ComponentRegistry::component_storage(ComponentTypeId type) const {
    auto found = storages.find(type);

    if (found == storages.end())
        throw std::runtime_error("component type storage not found");

    auto storage = std::dynamic_pointer_cast<const liquid::ComponentStorage<Component>>(found->second);

    if (!storage)
        throw std::runtime_error("component storage type mismatch");

    return storage;
}

template <typename Component>
ComponentType<Component> ComponentRegistry::register_component(TypeName typeName) {
    if (typeName.empty())
        throw std::runtime_error("component type name cannot be empty");

    if (componentTypes.contains(typeName))
        throw std::runtime_error("component type already registered");

    if (nextComponentType >= MaxComponentTypes)
        throw std::runtime_error("maximum component types reached");

    ComponentTypeId type = nextComponentType;
    auto storage = std::make_shared<liquid::ComponentStorage<Component>>();
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
        typeNames.push_back(typeName);
    } catch (...) {
        if (slotNamesCreated)
            slotNames.erase(type);

        if (componentNamesCreated)
            componentNames.erase(type);

        if (storageIndexed)
            storages.erase(type);

        if (typeIndexed)
            componentTypes.erase(typeName);

        throw;
    }

    ++nextComponentType;

    return ComponentType<Component>{type};
}

template <typename Component>
ComponentTypeId ComponentRegistry::component_type(ComponentType<Component> type) const {
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
    ComponentSlotId slot = storage->add(std::move(component));
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

        storage->remove(slot);
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

        return component_storage<Component>(typeId)->has(found->second);
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

    return component_storage<Component>(typeId)->get(slot);
}

template <typename Component>
const Component* ComponentRegistry::get_component_named(ComponentType<Component> type, const std::string& name) const {
    ComponentTypeId typeId = component_type(type);
    ComponentSlotId slot = named_slot(typeId, name);

    return component_storage<Component>(typeId)->get(slot);
}

template <typename Component>
void ComponentRegistry::remove_component(ComponentType<Component> type, const std::string& name) {
    ComponentTypeId typeId = component_type(type);
    ComponentSlotId slot = named_slot(typeId, name);

    auto storage = component_storage<Component>(typeId);
    // Clear every behavior reference to the slot before recycling it, then
    // erase the name indexes so the slot can safely be reused for a new name.
    storage->removeAccessesTo(slot);
    storage->remove(slot);

    componentNames[typeId].erase(name);
    slotNames[typeId].erase(slot);
}

template <typename Component>
void ComponentRegistry::grant_access(ComponentType<Component> type, BehaviorId behavior, const std::string& name, ComponentAccessMode mode) {
    ComponentTypeId typeId = component_type(type);
    ComponentSlotId slot = named_slot(typeId, name);
    auto storage = component_storage<Component>(typeId);
    liquid::ComponentSlotAccess::Mode storageMode = storage_access_mode(mode);

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
    for (const liquid::ComponentSlotAccess& access : behaviorAccesses->second) {
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
    return component_storage<Component>(component_type(type))->get(slot);
}

template <typename Component>
const Component* ComponentRegistry::resolve_component(ComponentType<Component> type, ComponentSlotId slot) const {
    return component_storage<Component>(component_type(type))->get(slot);
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

        if (!storage->has(slot))
            return false;

        const auto& allAccesses = storage->allAccesses();
        auto accesses = allAccesses.find(behavior);

        if (accesses == allAccesses.end())
            return false;

        for (const liquid::ComponentSlotAccess& access : accesses->second) {
            if (access.slot == slot && (access.mode == liquid::ComponentSlotAccess::r || access.mode == liquid::ComponentSlotAccess::rw))
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

        if (!storage->has(slot))
            return false;

        const auto& allAccesses = storage->allAccesses();
        auto accesses = allAccesses.find(behavior);

        if (accesses == allAccesses.end())
            return false;

        for (const liquid::ComponentSlotAccess& access : accesses->second) {
            if (access.slot == slot && (access.mode == liquid::ComponentSlotAccess::w || access.mode == liquid::ComponentSlotAccess::rw))
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
