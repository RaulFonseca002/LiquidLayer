#include "liquid/detail/IntentRegistry.hpp"

#include <limits>
#include <stdexcept>

namespace liquid::detail {

namespace {

int priority_value(IntentPriority priority) {
    return static_cast<int>(priority);
}

}

IntentRegistry::IntentRegistry(WorldInstanceId world)
    : worldId(world),
      generations(MaxIntents + 1, 1) {
    availableSlots.reserve(MaxIntents);
}

void IntentRegistry::ensure_no_active_transaction() const {
    if (transactionActive)
        throw std::logic_error(
            "ordinary intent mutation is forbidden during a transaction");
}

IntentRegistry::Transaction::Transaction(
    IntentRegistry& owner,
    std::size_t cancellationCount
)
    : registry(&owner),
      storages(owner.storages),
      intentTypes(owner.intentTypes),
      availableSlots(owner.availableSlots),
      generations(owner.generations),
      nextSlot(owner.nextSlot),
      lastSequence(owner.lastSequence),
      lifecycleRecordCount(owner.lifecycleRecords.size()),
      byOwner(owner.byOwner),
      byOwnerName(owner.byOwnerName),
      byTarget(owner.byTarget) {
    cancelled.reserve(cancellationCount);
}

IntentRegistry::Transaction::~Transaction() {
    if (active)
        registry->rollback(*this);
}

std::unique_ptr<IntentRegistry::Transaction> IntentRegistry::begin_transaction(
    std::size_t cancellationCount
) {
    if (transactionActive)
        throw std::logic_error("intent transaction is already active");
    auto transaction = std::unique_ptr<Transaction>(
        new Transaction(*this, cancellationCount));
    transactionActive = true;
    return transaction;
}

void IntentRegistry::cancel(Transaction& transaction, IntentId id) {
    if (transaction.registry != this || !transaction.active || !transactionActive)
        throw std::logic_error("intent transaction is not active");

    Intent removed = intent(id);
    const ComponentTypeId type = intentTypes.at(id);
    constexpr std::size_t maximumBufferedLifecycleRecords = 1'000'000;
    if (lifecycleRecords.size() >= maximumBufferedLifecycleRecords)
        throw std::length_error("intent lifecycle evidence capacity exceeded");
    lifecycleRecords.push_back({false, removed});

    std::shared_ptr<I_IntentStorage> storage = storages.at(type);
    std::unique_ptr<I_IntentStorage::Node> node = storage->extract(id);
    transaction.cancelled.push_back({std::move(storage), std::move(node)});
    erase_from_indexes(removed);
    intentTypes.erase(id);
    retire_intent_id(id);
}

void IntentRegistry::commit(Transaction& transaction) {
    if (transaction.registry != this || !transaction.active || !transactionActive)
        throw std::logic_error("intent transaction is not active");
    transaction.active = false;
    transactionActive = false;
    transaction.cancelled.clear();
}

void IntentRegistry::rollback(Transaction& transaction) noexcept {
    if (transaction.registry != this || !transaction.active || !transactionActive)
        std::terminate();

    for (const auto& [id, type] : intentTypes) {
        if (!transaction.intentTypes.contains(id))
            storages.at(type)->destroy(id);
    }
    for (auto& cancelled : transaction.cancelled)
        cancelled.storage->restore(std::move(cancelled.node));

    storages.swap(transaction.storages);
    intentTypes.swap(transaction.intentTypes);
    availableSlots.swap(transaction.availableSlots);
    generations.swap(transaction.generations);
    byOwner.swap(transaction.byOwner);
    byOwnerName.swap(transaction.byOwnerName);
    byTarget.swap(transaction.byTarget);
    nextSlot = transaction.nextSlot;
    lastSequence = transaction.lastSequence;
    lifecycleRecords.erase(
        lifecycleRecords.begin() +
            static_cast<std::ptrdiff_t>(transaction.lifecycleRecordCount),
        lifecycleRecords.end());
    transaction.active = false;
    transactionActive = false;
}

void IntentRegistry::validate_metadata(IntentLifetime lifetime, IntentPriority priority) {
    if (lifetime.kind != IntentLifetimeKind::Persistent &&
        lifetime.kind != IntentLifetimeKind::UntilTime) {
        throw std::invalid_argument("unknown intent lifetime kind");
    }

    if (lifetime.kind == IntentLifetimeKind::Persistent && lifetime.expiresAt != 0)
        throw std::invalid_argument("persistent intent lifetime must not expire");

    if (priority != IntentPriority::Low &&
        priority != IntentPriority::Medium &&
        priority != IntentPriority::High) {
        throw std::invalid_argument("unknown intent priority");
    }
}

void IntentRegistry::release_intent_id(IntentId id) {
    if (id.slot + 1 == nextSlot) {
        --nextSlot;
        return;
    }

    availableSlots.push_back(id.slot);
}

void IntentRegistry::retire_intent_id(IntentId id) {
    if (generations.at(id.slot) == std::numeric_limits<std::uint32_t>::max())
        return;

    ++generations.at(id.slot);
    availableSlots.push_back(id.slot);
}

IntentId IntentRegistry::next_intent_id() {
    if (intentTypes.size() >= MaxIntents && availableSlots.empty())
        throw std::runtime_error("all the intents are already in use");

    if (!availableSlots.empty()) {
        std::uint32_t slot = availableSlots.back();
        availableSlots.pop_back();
        return IntentId{worldId, slot, generations.at(slot)};
    }

    std::uint32_t slot = nextSlot++;
    return IntentId{worldId, slot, generations.at(slot)};
}

liquid::IntentSequence IntentRegistry::next_intent_sequence() {
    if (lastSequence == std::numeric_limits<liquid::IntentSequence>::max())
        throw std::overflow_error("intent sequence exhausted");

    return ++lastSequence;
}

const I_IntentStorage& IntentRegistry::storage_for(IntentId id) const {
    auto type = intentTypes.find(id);

    if (type == intentTypes.end())
        throw std::runtime_error("intent id not found");

    auto storage = storages.find(type->second);

    if (storage == storages.end())
        throw std::runtime_error("intent storage type not found");

    return *storage->second;
}

void IntentRegistry::erase_from_indexes(const Intent& intent) {
    auto owner = byOwner.find(intent.owner);

    if (owner != byOwner.end())
        owner->second.erase(intent.id);
    if (!intent.name.empty()) {
        auto names = byOwnerName.find(intent.owner);
        if (names != byOwnerName.end())
            names->second.erase(intent.name);
    }

    auto type = byTarget.find(intent.target.type);

    if (type == byTarget.end())
        return;

    auto slot = type->second.find(intent.target.slot);

    if (slot == type->second.end())
        return;

    slot->second.erase(intent.id);

    if (slot->second.empty())
        type->second.erase(slot);

    if (type->second.empty())
        byTarget.erase(type);
}

void IntentRegistry::destroy(IntentId id) {
    ensure_no_active_transaction();
    Intent removed = intent(id);
    ComponentTypeId type = intentTypes.at(id);

    constexpr std::size_t maximumBufferedLifecycleRecords = 1'000'000;
    if (lifecycleRecords.size() >= maximumBufferedLifecycleRecords)
        throw std::length_error("intent lifecycle evidence capacity exceeded");
    lifecycleRecords.push_back({false, removed});
    erase_from_indexes(removed);
    storages.at(type)->destroy(id);
    intentTypes.erase(id);
    retire_intent_id(id);
}

const std::vector<IntentLifecycleRecord>& IntentRegistry::lifecycle_records() const {
    return lifecycleRecords;
}

void IntentRegistry::clear_lifecycle_records() {
    ensure_no_active_transaction();
    lifecycleRecords.clear();
}

void IntentRegistry::destroy_owned_by(BehaviorId owner) {
    ensure_no_active_transaction();
    auto found = byOwner.find(owner);

    if (found == byOwner.end())
        return;

    std::vector<IntentId> owned(found->second.begin(), found->second.end());

    for (IntentId id : owned)
        destroy(id);

    byOwner.erase(owner);
}

bool IntentRegistry::exists(IntentId id) const {
    return intentTypes.contains(id);
}

const Intent& IntentRegistry::intent(IntentId id) const {
    return storage_for(id).intent(id);
}

BehaviorId IntentRegistry::owner_of(IntentId id) const {
    return intent(id).owner;
}

ComponentTarget IntentRegistry::target_of(IntentId id) const {
    return intent(id).target;
}

IntentLifetime IntentRegistry::lifetime_of(IntentId id) const {
    return intent(id).lifetime;
}

std::vector<IntentId> IntentRegistry::live_intent_ids() const {
    std::vector<IntentId> live;
    live.reserve(intentTypes.size());

    for (const auto& [id, type] : intentTypes) {
        (void)type;
        live.push_back(id);
    }

    return live;
}

std::vector<IntentId> IntentRegistry::intents_owned_by(
    BehaviorId owner
) const {
    const auto found = byOwner.find(owner);
    if (found == byOwner.end())
        return {};
    return {found->second.begin(), found->second.end()};
}

std::optional<IntentId> IntentRegistry::intent_named(
    BehaviorId owner,
    const IntentName& name
) const {
    const auto owners = byOwnerName.find(owner);
    if (owners == byOwnerName.end())
        return std::nullopt;
    const auto found = owners->second.find(name);
    if (found == owners->second.end())
        return std::nullopt;
    return found->second;
}

std::vector<IntentId> IntentRegistry::intents_for(ComponentTypeId type, ComponentSlotId slot) const {
    auto typePosition = byTarget.find(type);

    if (typePosition == byTarget.end())
        return {};

    auto slotPosition = typePosition->second.find(slot);

    if (slotPosition == typePosition->second.end())
        return {};

    return {slotPosition->second.begin(), slotPosition->second.end()};
}

const IntentTargetIndex& IntentRegistry::target_index() const {
    return byTarget;
}

std::map<ComponentName, IntentId> IntentRegistry::select(
    ComponentTypeId type,
    const std::map<ComponentName, ComponentSlotId>& components
) const {
    std::map<ComponentName, IntentId> selected;
    auto typePosition = byTarget.find(type);

    for (const auto& [name, slot] : components) {
        if (typePosition == byTarget.end())
            break;

        auto slotPosition = typePosition->second.find(slot);

        if (slotPosition == typePosition->second.end())
            continue;

        IntentId selectedIntent = 0;
        liquid::IntentSequence selectedSequence = 0;
        IntentPriority selectedPriority = IntentPriority::Low;
        bool hasSelection = false;

        for (IntentId id : slotPosition->second) {
            const Intent& candidate = intent(id);

            if (!hasSelection ||
                priority_value(candidate.priority) > priority_value(selectedPriority) ||
                (candidate.priority == selectedPriority && candidate.sequence > selectedSequence)) {
                selectedIntent = id;
                selectedPriority = candidate.priority;
                selectedSequence = candidate.sequence;
                hasSelection = true;
            }
        }

        if (hasSelection)
            selected.emplace(name, selectedIntent);
    }

    return selected;
}

std::map<ComponentName, IntentId> IntentRegistry::resolve(
    ComponentTypeId type,
    const std::map<ComponentName, ComponentSlotId>& components,
    IntentTime now
) {
    ensure_no_active_transaction();
    std::vector<IntentId> expired;

    for (IntentId id : live_intent_ids()) {
        IntentLifetime lifetime = lifetime_of(id);

        if (lifetime.kind == IntentLifetimeKind::UntilTime && now >= lifetime.expiresAt)
            expired.push_back(id);
    }

    for (IntentId id : expired)
        destroy(id);

    return select(type, components);
}

std::size_t IntentRegistry::size(BehaviorId id) const {
    auto ownerPosition = byOwner.find(id);

    if (ownerPosition == byOwner.end())
        return 0;

    return ownerPosition->second.size();
}

std::size_t IntentRegistry::size() const {
    return intentTypes.size();
}

void IntentRegistry::create_behavior_pool(BehaviorId id) {
    destroy_owned_by(id);
    byOwner[id];
    byOwnerName[id];
}

}
