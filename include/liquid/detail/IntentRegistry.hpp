#pragma once

#include "liquid/Intent.hpp"

#include <cstddef>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace liquid::detail {

struct IntentLifecycleRecord {
    bool created = false;
    Intent intent;
};

class I_IntentStorage {
public:
    virtual ~I_IntentStorage() = default;

    virtual void destroy(IntentId id) = 0;
    virtual bool exists(IntentId id) const = 0;
    virtual const Intent& intent(IntentId id) const = 0;
};

template <typename Component>
class IntentStorage : public I_IntentStorage {
private:
    std::map<IntentId, ComponentIntent<Component>> records;

public:
    void add(ComponentIntent<Component>&& intent) {
        auto [position, inserted] = records.emplace(intent.id, std::move(intent));
        (void)position;

        if (!inserted)
            throw std::logic_error("intent id already exists in typed storage");
    }

    void destroy(IntentId id) override {
        records.erase(id);
    }

    bool exists(IntentId id) const override {
        return records.contains(id);
    }

    const Intent& intent(IntentId id) const override {
        auto found = records.find(id);

        if (found == records.end())
            throw std::runtime_error("intent id not found");

        return found->second;
    }

    const ComponentIntent<Component>& typed_intent(IntentId id) const {
        auto found = records.find(id);

        if (found == records.end())
            throw std::runtime_error("intent id not found");

        return found->second;
    }
};

class IntentRegistry {
private:
    WorldInstanceId worldId = 0;
    std::map<ComponentTypeId, std::shared_ptr<I_IntentStorage>> storages;
    std::map<IntentId, ComponentTypeId> intentTypes;
    std::vector<std::uint32_t> availableSlots;
    std::vector<std::uint32_t> generations;
    std::uint32_t nextSlot = 1;
    liquid::IntentSequence lastSequence = 0;
    std::vector<IntentLifecycleRecord> lifecycleRecords;

    std::map<BehaviorId, std::set<IntentId>> byOwner;
    IntentTargetIndex byTarget;

    IntentId next_intent_id();
    liquid::IntentSequence next_intent_sequence();
    void release_intent_id(IntentId id);
    void retire_intent_id(IntentId id);
    static void validate_metadata(IntentLifetime lifetime, IntentPriority priority);
    const I_IntentStorage& storage_for(IntentId id) const;
    void erase_from_indexes(const Intent& intent);

    template <typename Component>
    std::shared_ptr<IntentStorage<Component>> storage_for(ComponentType<Component> type);

    template <typename Component>
    std::shared_ptr<const IntentStorage<Component>> storage_for(ComponentType<Component> type) const;

public:
    explicit IntentRegistry(WorldInstanceId world = 0);
    IntentRegistry(const IntentRegistry&) = delete;
    IntentRegistry& operator=(const IntentRegistry&) = delete;
    IntentRegistry(IntentRegistry&&) = delete;
    IntentRegistry& operator=(IntentRegistry&&) = delete;

    template <typename Component>
    IntentId create(
        BehaviorId owner,
        ComponentType<Component> type,
        ComponentSlotId slot,
        IntentLifetime lifetime,
        Component value,
        IntentPriority priority = IntentPriority::Medium,
        liquid::Value encodedValue = liquid::Value{}
    );

    void destroy(IntentId id);
    void destroy_owned_by(BehaviorId owner);

    bool exists(IntentId id) const;
    const Intent& intent(IntentId id) const;

    template <typename Component>
    const ComponentIntent<Component>& typed_intent(ComponentType<Component> type, IntentId id) const;

    BehaviorId owner_of(IntentId id) const;
    ComponentTarget target_of(IntentId id) const;
    IntentLifetime lifetime_of(IntentId id) const;

    std::vector<IntentId> live_intent_ids() const;
    std::vector<IntentId> intents_for(ComponentTypeId type, ComponentSlotId slot) const;
    const IntentTargetIndex& target_index() const;
    std::map<ComponentName, IntentId> select(ComponentTypeId type, const std::map<ComponentName, ComponentSlotId>& components) const;
    std::map<ComponentName, IntentId> resolve(ComponentTypeId type, const std::map<ComponentName, ComponentSlotId>& components, IntentTime now);

    std::size_t size(BehaviorId id) const;
    std::size_t size() const;
    void create_behavior_pool(BehaviorId id);
    const std::vector<IntentLifecycleRecord>& lifecycle_records() const;
    void clear_lifecycle_records();
};

template <typename Component>
std::shared_ptr<IntentStorage<Component>> IntentRegistry::storage_for(ComponentType<Component> type) {
    if (type.id == InvalidComponentTypeId)
        throw std::runtime_error("invalid component type id");

    auto found = storages.find(type.id);

    if (found == storages.end()) {
        auto storage = std::make_shared<IntentStorage<Component>>();
        storages.emplace(type.id, storage);
        return storage;
    }

    auto storage = std::dynamic_pointer_cast<IntentStorage<Component>>(found->second);

    if (!storage)
        throw std::runtime_error("intent storage type mismatch");

    return storage;
}
template <typename Component>
std::shared_ptr<const IntentStorage<Component>> IntentRegistry::storage_for(ComponentType<Component> type) const {
    auto found = storages.find(type.id);

    if (found == storages.end())
        throw std::runtime_error("intent storage type not found");

    auto storage = std::dynamic_pointer_cast<const IntentStorage<Component>>(found->second);

    if (!storage)
        throw std::runtime_error("intent storage type mismatch");

    return storage;
}

template <typename Component>
IntentId IntentRegistry::create(BehaviorId owner, ComponentType<Component> type, ComponentSlotId slot, IntentLifetime lifetime, Component value, IntentPriority priority, liquid::Value encodedValue) {
    if (!byOwner.contains(owner))
        throw std::runtime_error("behavior intent pool not found");

    if (type.id == InvalidComponentTypeId)
        throw std::runtime_error("invalid component type id");

    if (slot == InvalidComponentSlotId)
        throw std::runtime_error("invalid component slot id");

    validate_metadata(lifetime, priority);

    std::shared_ptr<IntentStorage<Component>> storage = storage_for(type);
    IntentId id = next_intent_id();
    liquid::IntentSequence sequence = next_intent_sequence();
    bool storageAdded = false;
    bool typeIndexed = false;
    bool ownerIndexed = false;

    try {
        ComponentIntent<Component> record{
            {id, owner, {type.id, slot}, lifetime, priority, sequence, std::move(encodedValue)},
            std::move(value)
        };
        storage->add(std::move(record));
        storageAdded = true;

        auto [typePosition, typeInserted] = intentTypes.emplace(id, type.id);
        (void)typePosition;

        if (!typeInserted)
            throw std::logic_error("intent id already indexed");

        typeIndexed = true;

        auto [ownerPosition, ownerInserted] = byOwner.at(owner).emplace(id);
        (void)ownerPosition;

        if (!ownerInserted)
            throw std::logic_error("intent id already indexed by owner");

        ownerIndexed = true;

        auto [targetPosition, targetInserted] = byTarget[type.id][slot].emplace(id);
        (void)targetPosition;

        if (!targetInserted)
            throw std::logic_error("intent id already indexed by target");

        constexpr std::size_t maximumBufferedLifecycleRecords = 1'000'000;
        if (lifecycleRecords.size() >= maximumBufferedLifecycleRecords)
            throw std::length_error("intent lifecycle evidence capacity exceeded");
        lifecycleRecords.push_back({true, storage->intent(id)});
        return id;
    } catch (...) {
        auto targetType = byTarget.find(type.id);

        if (targetType != byTarget.end()) {
            auto targetSlot = targetType->second.find(slot);

            if (targetSlot != targetType->second.end()) {
                targetSlot->second.erase(id);

                if (targetSlot->second.empty())
                    targetType->second.erase(targetSlot);
            }

            if (targetType->second.empty())
                byTarget.erase(targetType);
        }

        if (ownerIndexed)
            byOwner.at(owner).erase(id);

        if (typeIndexed)
            intentTypes.erase(id);

        if (storageAdded)
            storage->destroy(id);

        release_intent_id(id);
        throw;
    }
}

template <typename Component>
const ComponentIntent<Component>& IntentRegistry::typed_intent(ComponentType<Component> type, IntentId id) const {
    auto found = intentTypes.find(id);

    if (found == intentTypes.end())
        throw std::runtime_error("intent id not found");

    if (found->second != type.id)
        throw std::runtime_error("intent component type mismatch");

    return storage_for(type)->typed_intent(id);
}

}
