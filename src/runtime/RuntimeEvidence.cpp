#include "liquid/Runtime.hpp"
#include "RuntimeInternals.hpp"

#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace liquid::detail {

namespace {


std::pair<std::string, Value> serialized_topology(
    const TopologyMutation& topology,
    WorldInstanceId runtimeWorld,
    SessionId session
) {
    std::string key = topology.key;
    const std::string world = std::to_string(runtimeWorld);
    const std::string logicalWorld = std::to_string(session.value);
    for (const std::string prefix : {"behavior:", "access:"}) {
        const std::string actualPrefix = prefix + world + ":";
        if (key.starts_with(actualPrefix)) {
            key.replace(prefix.size(), world.size(), logicalWorld);
            break;
        }
    }

    Value value = topology.value;
    if (value.kind() == Value::Kind::Object) {
        Value::Object normalized = value.as_object();
        for (const char* field : {"world", "slot_world", "behavior_world"}) {
            const auto found = normalized.find(field);
            if (found != normalized.end())
                found->second = Value{session.value};
        }
        value = Value{std::move(normalized)};
    }
    return {std::move(key), std::move(value)};
}

}

}

namespace liquid {

void Runtime::record_world_evidence() {
    for (const auto& topology : ownedWorld.topology_mutations()) {
        auto [key, value] = liquid::detail::serialized_topology(
            topology, ownedWorld.instance_id(), effectsState->session_id());
        effectsState->append(liquid::EventType::TopologyChanged,
            liquid::detail::event_payload({
                {"key", liquid::Value{std::move(key)}},
                {"value", std::move(value)},
                {"removed", liquid::Value{topology.removed}}
            }));
    }
    ownedWorld.clear_topology_mutations();
    for (const auto& mutation : ownedWorld.component_mutations()) {
        effectsState->append(
            mutation.removed
                ? liquid::EventType::ComponentRemoved
                : liquid::EventType::ComponentMutated,
            liquid::detail::event_payload({
                {"key", liquid::Value{
                    std::to_string(mutation.target.type) + ":" + mutation.name}},
                {"value", mutation.after},
                {"type", liquid::Value{
                    static_cast<std::uint64_t>(mutation.target.type)}},
                {"slot_world", liquid::Value{effectsState->session_id().value}},
                {"slot", liquid::Value{
                    static_cast<std::uint64_t>(mutation.target.slot.slot)}},
                {"generation", liquid::Value{
                    static_cast<std::uint64_t>(mutation.target.slot.generation)}},
                {"name", liquid::Value{mutation.name}},
                {"before", mutation.before},
                {"after", mutation.after}
            }));
    }
    ownedWorld.clear_component_mutations();

    for (const auto& lifecycle : ownedWorld.intent_lifecycle_records()) {
        const auto& intent = lifecycle.intent;
        const std::string key = "intent:" +
            std::to_string(effectsState->session_id().value) + ":" +
            std::to_string(intent.id.slot) + ":" +
            std::to_string(intent.id.generation);
        if (!lifecycle.created) {
            effectsState->append(liquid::EventType::IntentDestroyed,
                liquid::detail::event_payload({
                    {"key", liquid::Value{key}},
                    {"intent_world", liquid::Value{effectsState->session_id().value}},
                    {"intent_slot", liquid::Value{
                        static_cast<std::uint64_t>(intent.id.slot)}},
                    {"intent_generation", liquid::Value{
                        static_cast<std::uint64_t>(intent.id.generation)}}
                }));
            continue;
        }
        effectsState->append(liquid::EventType::IntentCreated,
            liquid::detail::event_payload({
                {"key", liquid::Value{key}},
                {"value", intent.encodedValue},
                {"intent_world", liquid::Value{effectsState->session_id().value}},
                {"intent_slot", liquid::Value{
                    static_cast<std::uint64_t>(intent.id.slot)}},
                {"intent_generation", liquid::Value{
                    static_cast<std::uint64_t>(intent.id.generation)}},
                {"owner_world", liquid::Value{effectsState->session_id().value}},
                {"owner_slot", liquid::Value{
                    static_cast<std::uint64_t>(intent.owner.slot)}},
                {"owner_generation", liquid::Value{
                    static_cast<std::uint64_t>(intent.owner.generation)}},
                {"type", liquid::Value{
                    static_cast<std::uint64_t>(intent.target.type)}},
                {"target_world", liquid::Value{effectsState->session_id().value}},
                {"target_slot", liquid::Value{
                    static_cast<std::uint64_t>(intent.target.slot.slot)}},
                {"target_generation", liquid::Value{
                    static_cast<std::uint64_t>(intent.target.slot.generation)}},
                {"priority", liquid::Value{
                    static_cast<std::uint64_t>(intent.priority)}},
                {"name", liquid::Value{intent.name}},
                {"sequence", liquid::Value{intent.sequence}},
                {"lifetime", liquid::Value{
                    intent.lifetime.kind == IntentLifetimeKind::Persistent
                        ? "persistent" : "until-time"}},
                {"expires_at", liquid::Value{intent.lifetime.expiresAt}}
            }));
    }
    ownedWorld.clear_intent_lifecycle_records();

    // GCOVR_EXCL_START: script evidence is produced only through the Lua
    // boundary, which the Core-only coverage gate excludes by design; the
    // Lua-enabled suite (test_lua_lifecycle, test_lua_behavior) covers it.
    for (const auto& script : ownedWorld.script_execution_evidence()) {
        liquid::Value::Object payload;
        payload.emplace("key", liquid::Value{
            "script:" + std::to_string(effectsState->session_id().value) + ":" +
            std::to_string(script.owner.slot) + ":" +
            std::to_string(script.owner.generation) + ":" +
            std::to_string(script.now)});
        payload.emplace("owner_world", liquid::Value{effectsState->session_id().value});
        payload.emplace("owner_slot", liquid::Value{
            static_cast<std::uint64_t>(script.owner.slot)});
        payload.emplace("owner_generation", liquid::Value{
            static_cast<std::uint64_t>(script.owner.generation)});
        payload.emplace("now", liquid::Value{script.now});
        payload.emplace("source_included", liquid::Value{script.sourceIncluded});
        payload.emplace("source_hash", liquid::Value{script.sourceHash});
        if (script.sourceIncluded)
            payload.emplace("source", liquid::Value{script.source});
        payload.emplace("status", liquid::Value{
            static_cast<std::uint64_t>(script.status)});
        payload.emplace("diagnostic", liquid::Value{script.diagnostic});
        payload.emplace("created_intents", liquid::Value{
            static_cast<std::uint64_t>(script.createdIntentCount)});
        effectsState->append(
            liquid::EventType::ScriptExecuted,
            liquid::Value{std::move(payload)});
    }
    // GCOVR_EXCL_STOP
    ownedWorld.clear_script_execution_evidence();
}

std::vector<ResolvedEffect> Runtime::apply_selections(
    const FrameLog& frame
) {
    std::vector<ResolvedEffect> effects;
    std::set<ComponentTarget> selectedExternalComponents;
    for (const auto& [type, selections] : frame.intent_selections) {
        for (const auto& [name, id] : selections) {
            const Intent& selected = ownedWorld.intent(id);
            const ComponentTarget target = selected.target;
            const auto configured = componentControls.find(target);
            const ComponentControl control = configured == componentControls.end()
                ? ComponentControl::SelectionOnly
                : configured->second;

            if (control == ComponentControl::SelectionOnly)
                continue;
            if (control == ComponentControl::InternalState) {
                ownedWorld.replace_component_value(
                    target, name, selected.encodedValue);
                continue;
            }

            const auto binding = effectBindings.find(target);
            if (binding == effectBindings.end())
                throw std::logic_error(
                    "external component selection has no effect binding");
            auto effect = ownedWorld.encode_effect(
                target, name, selected.encodedValue);
            if (!effect)
                continue;
            if (effect->adapterRoute != binding->second.route ||
                effect->target != binding->second.target) {
                throw std::invalid_argument(
                    "effect codec output does not match the stable binding");
            }
            selectedExternalComponents.insert(target);
            effects.push_back(std::move(*effect));
        }
    }
    for (const auto& [target, binding] : effectBindings) {
        if (!selectedExternalComponents.contains(target))
            effectsState->clear_desire(binding.route, binding.target);
    }
    return effects;
}

void Runtime::project_authoritative_reports(
    const std::vector<EffectReport>& reports
) {
    for (const EffectReport& report : reports) {
        if (!effectsState->report_is_authoritative(report))
            continue;
        const auto bound = componentsByEffectTarget.find({
            report.adapterRoute.value(), report.target.value()});
        if (bound == componentsByEffectTarget.end())
            continue;
        const auto binding = effectBindings.find(bound->second);
        if (binding == effectBindings.end())
            throw std::logic_error("effect target binding is inconsistent");
        Value projected = ownedWorld.decode_observed(
            binding->second.component, *report.observedValue);
        ownedWorld.replace_component_value(
            binding->second.component,
            binding->second.name,
            projected);
    }
}

void Runtime::project_authoritative_observations(
    const std::vector<ExternalObservation>& observations
) {
    for (const ExternalObservation& observation : observations) {
        if (!effectsState->observation_is_authoritative(observation))
            continue;
        const auto bound = componentsByEffectTarget.find({
            observation.adapterRoute.value(), observation.target.value()});
        if (bound == componentsByEffectTarget.end())
            continue;
        const auto binding = effectBindings.find(bound->second);
        if (binding == effectBindings.end())
            throw std::logic_error("effect target binding is inconsistent");
        Value projected = ownedWorld.decode_observed(
            binding->second.component, observation.observedValue);
        ownedWorld.replace_component_value(
            binding->second.component,
            binding->second.name,
            projected);
    }
}

}
