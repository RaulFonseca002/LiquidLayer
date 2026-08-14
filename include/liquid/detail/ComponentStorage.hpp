#pragma once

#include "liquid/Ids.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace liquid::detail {

// Access is local to one typed storage. The slot identifies the component
// instance inside this storage; ComponentRegistry owns the name/type lookup
// needed to find that slot.
struct ComponentSlotAccess {

    enum Mode {
        r,
        rw,
        w
    };

    Mode mode;
    ComponentSlotId slot;
};

class IComponentStorage {
public:
    virtual ~IComponentStorage() = default;

    virtual const std::unordered_map<BehaviorId, std::vector<ComponentSlotAccess>>& allAccesses() const = 0;
    virtual const std::vector<ComponentSlotAccess>& accessesOf(BehaviorId behavior) const = 0;
    // Cleanup hooks used by ComponentRegistry/Coordinator before slots or
    // behavior IDs are recycled.
    virtual void removeAccess(BehaviorId behavior, ComponentSlotId slot) = 0;
    virtual void removeAccessesOf(BehaviorId behavior) = 0;
    virtual void removeAccessesTo(ComponentSlotId slot) = 0;
};

template<typename Component>
class ComponentStorage : public IComponentStorage{
private:
    // One ComponentStorage<T> stores only T instances. Names, type IDs, and
    // cross-manager permission checks belong to ComponentRegistry/Coordinator.
    std::vector<std::optional<Component>> components;
    std::vector<Slot> availableSlots;
    std::unordered_map<BehaviorId, std::vector<ComponentSlotAccess>> accesses;
    std::size_t count = 0;

public:

    ComponentStorage() {
        availableSlots.reserve(MaxComponentSlots);
    }

    Slot add(Component component) {

        Slot id = 0;

        if (!availableSlots.empty()) {
            id = availableSlots.back();
            components[id].emplace(std::move(component));
            availableSlots.pop_back();
        }else{

            if (components.size() >= MaxComponentSlots)
                throw std::runtime_error("all component slots are already filled");

            id = static_cast<Slot>(components.size());
            components.emplace_back(std::in_place, std::move(component));
        }

        count++;
        return id;
    }

    void remove(Slot id, bool recycle = true) {
        if (id >= components.size())
            throw std::out_of_range("invalid component slot id");

        if (!components[id].has_value())
            throw std::runtime_error("component slot already removed");

        if (recycle)
            availableSlots.push_back(id);
        components[id].reset();
        removeAccessesTo(id);
        count--;
    }

    void addAccess(BehaviorId behavior, ComponentSlotAccess::Mode mode, ComponentSlotId slot) {
        if (!has(slot))
            throw std::runtime_error("component slot not found");

        if (mode != ComponentSlotAccess::r &&
            mode != ComponentSlotAccess::w &&
            mode != ComponentSlotAccess::rw) {
            throw std::runtime_error("unknown component access mode");
        }

        accesses[behavior].push_back({mode, slot});
    }

    void removeAccess(BehaviorId behavior, ComponentSlotId slot) override {
        auto it = accesses.find(behavior);

        if (it == accesses.end())
            return;

        auto& behaviorAccesses = it->second;
        behaviorAccesses.erase(
            std::remove_if(behaviorAccesses.begin(), behaviorAccesses.end(), [slot](const ComponentSlotAccess& access) {
                return access.slot == slot;
            }),
            behaviorAccesses.end());

        if (behaviorAccesses.empty())
            accesses.erase(it);
    }

    void removeAccessesOf(BehaviorId behavior) override {
        accesses.erase(behavior);
    }

    void removeAccessesTo(ComponentSlotId slot) override {
        for (auto it = accesses.begin(); it != accesses.end();) {
            auto& behaviorAccesses = it->second;
            behaviorAccesses.erase(
                std::remove_if(behaviorAccesses.begin(), behaviorAccesses.end(), [slot](const ComponentSlotAccess& access) {
                    return access.slot == slot;
                }),
                behaviorAccesses.end());

            if (behaviorAccesses.empty()) {
                it = accesses.erase(it);
            } else {
                ++it;
            }
        }
    }

    const std::unordered_map<BehaviorId, std::vector<ComponentSlotAccess>>& allAccesses() const override {
        return accesses;
    }

    const std::vector<ComponentSlotAccess>& accessesOf(BehaviorId behavior) const override {
        auto it = accesses.find(behavior);

        if (it == accesses.end())
            throw std::runtime_error("behavior has no access to this component storage");

        return it->second;
    }

    Component& operator[](Slot id) {
        if (id >= components.size())
            throw std::out_of_range("invalid component slot id");

        if (!components[id].has_value())
            throw std::runtime_error("component slot removed");

        return *components[id];
    }

    const Component& operator[](Slot id) const {
        if (id >= components.size())
            throw std::out_of_range("invalid component slot id");
        if (!components[id].has_value())
            throw std::runtime_error("component slot removed");

        return *components[id];
    }

    void replace(Slot id, Component component) {
        static_assert(
            std::is_nothrow_swappable_v<Component>,
            "transactional component replacement requires nothrow swap"
        );

        if (!has(id))
            throw std::runtime_error("component slot removed");

        using std::swap;
        swap(*components[id], component);
    }

    bool has(Slot id) const {
        if (id >= components.size())
            return false;

        return components[id].has_value();
    }

    // Returned references and pointers are borrowed views. They remain valid
    // only until the next structural mutation of this typed storage and must
    // not be retained across frames or exposed to scripting boundaries.
    Component* get(Slot id) {
        if (!has(id))
            return nullptr;

        return &*components[id];
    }

    const Component* get(Slot id) const {
        if (!has(id))
            return nullptr;

        return &*components[id];
    }

    std::size_t size() const {
        return count;
    }

    std::size_t slot_count() const {
        return components.size();
    }

};

}
