#include "liquid/world/Coordinator.hpp"

#include <exception>
#include <limits>
#include <stdexcept>

Coordinator::Coordinator(WorldState& worldState)
    : state(worldState)
{
}

BehaviorId Coordinator::create_behavior() {
    BehaviorAccessRevision accessRevision = next_behavior_access_revision();
    BehaviorId behavior = state.behaviors.create();

    try {
        state.intents.create_behavior_pool(behavior);
        auto [signaturePosition, signatureInserted] = state.behaviorSignatures.emplace(
            behavior,
            Signature{}
        );
        (void)signaturePosition;

        if (!signatureInserted)
            throw std::logic_error("behavior signature already exists");

        auto [revisionPosition, revisionInserted] = state.behaviorAccessRevisions.emplace(
            behavior,
            accessRevision
        );
        (void)revisionPosition;

        if (!revisionInserted)
            throw std::logic_error("behavior access revision already exists");

        update_system_memberships(behavior);
    } catch (...) {
        std::exception_ptr creationException = std::current_exception();

        try {
            state.systems.remove_behavior(behavior);
        } catch (...) {
        }

        state.behaviorSignatures.erase(behavior);
        state.behaviorAccessRevisions.erase(behavior);
        state.intents.destroy_owned_by(behavior);
        state.behaviors.destroy(behavior);
        std::rethrow_exception(creationException);
    }

    return behavior;
}

void Coordinator::destroy_behavior(BehaviorId id) {
    if (!state.behaviors.exists(id))
        throw std::runtime_error("behavior id not found");

    state.components.remove_behavior(id);
    state.behaviorSignatures.erase(id);
    state.behaviorAccessRevisions.erase(id);
    std::exception_ptr callbackException;

    try {
        state.systems.remove_behavior(id);
    } catch (...) {
        callbackException = std::current_exception();
    }

    state.intents.destroy_owned_by(id);
    state.behaviors.destroy(id);

    if (callbackException)
        std::rethrow_exception(callbackException);
}

bool Coordinator::behavior_exists(BehaviorId id) {
    return state.behaviors.exists(id);
}

std::size_t Coordinator::behavior_count() {
    return state.behaviors.size();
}

BehaviorAccessRevision Coordinator::behavior_access_revision(BehaviorId id) const {
    if (!state.behaviors.exists(id))
        throw std::runtime_error("behavior id not found");

    auto found = state.behaviorAccessRevisions.find(id);

    if (found == state.behaviorAccessRevisions.end())
        throw std::logic_error("behavior access revision not found");

    return found->second;
}

BehaviorAccessRevision Coordinator::next_behavior_access_revision() {
    if (state.lastBehaviorAccessRevision == std::numeric_limits<BehaviorAccessRevision>::max())
        throw std::overflow_error("behavior access revision exhausted");

    return ++state.lastBehaviorAccessRevision;
}

void Coordinator::destroy_intent(IntentId id) {
    state.intents.destroy(id);
}

bool Coordinator::intent_exists(IntentId id) {
    return state.intents.exists(id);
}

BehaviorId Coordinator::intent_owner(IntentId id) {
    return state.intents.owner_of(id);
}

ComponentTarget Coordinator::intent_target(IntentId id) {
    return state.intents.target_of(id);
}

IntentLifetime Coordinator::intent_lifetime(IntentId id) {
    return state.intents.lifetime_of(id);
}

const Intent& Coordinator::intent(IntentId id) const {
    return state.intents.intent(id);
}

std::vector<IntentId> Coordinator::live_intent_ids() const {
    return state.intents.live_intent_ids();
}

std::vector<IntentId> Coordinator::intents_for(ComponentTypeId type, ComponentSlotId slot) const {
    return state.intents.intents_for(type, slot);
}

const IntentTargetIndex& Coordinator::intent_target_index() const {
    return state.intents.target_index();
}

std::map<ComponentName, IntentId> Coordinator::resolve_intents(
    ComponentTypeId type,
    const std::map<ComponentName, ComponentSlotId>& components,
    IntentTime now
) {
    (void)now;
    return state.intents.select(type, components);
}

void Coordinator::destroy_intents_for_target(ComponentTypeId type, ComponentSlotId slot) {
    std::vector<IntentId> targeted = state.intents.intents_for(type, slot);

    for (IntentId id : targeted)
        state.intents.destroy(id);
}

void Coordinator::destroy_intents_for_owner_target(BehaviorId owner, ComponentTypeId type, ComponentSlotId slot) {
    std::vector<IntentId> targeted = state.intents.intents_for(type, slot);

    for (IntentId id : targeted) {
        if (state.intents.owner_of(id) == owner)
            state.intents.destroy(id);
    }
}

std::size_t Coordinator::intent_count(BehaviorId owner) {
    if (!state.behaviors.exists(owner))
        throw std::runtime_error("behavior id not found");

    return state.intents.size(owner);
}

std::size_t Coordinator::system_count() const {
    return state.systems.size();
}

std::size_t Coordinator::run_systems(
    World& world,
    FrameNumber frame,
    IntentTime now,
    std::size_t* completedSystems
) {
    return state.systems.run_systems(world, frame, now, completedSystems);
}

Signature Coordinator::behavior_signature(BehaviorId behavior) const {
    auto found = state.behaviorSignatures.find(behavior);

    if (found == state.behaviorSignatures.end())
        return {};

    return found->second;
}

void Coordinator::update_system_memberships(BehaviorId behavior) {
    state.systems.update_behavior(behavior, behavior_signature(behavior));
}

void Coordinator::update_all_system_memberships() {
    std::exception_ptr firstException;

    for (const auto& [behavior, behaviorSignature] : state.behaviorSignatures) {
        (void)behaviorSignature;

        try {
            update_system_memberships(behavior);
        } catch (...) {
            if (!firstException)
                firstException = std::current_exception();
        }
    }

    if (firstException)
        std::rethrow_exception(firstException);
}
