#pragma once

#include "liquid/Ids.hpp"
#include "liquid/IntentLifetime.hpp"
#include "liquid/effects/EffectAdapter.hpp"
#include "liquid/events/EventStore.hpp"
#include "liquid/world/World.hpp"

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <thread>

namespace liquid {

struct FrameLog {
    FrameNumber frame = 0;
    IntentTime now = 0;
    bool completed = false;
    std::vector<std::string> phases;
    std::size_t expired_intents = 0;
    std::size_t resolution_requests = 0;
    std::size_t selected_intents = 0;
    std::size_t systems_run = 0;
    std::map<ComponentTypeId, std::map<ComponentName, IntentId>> intent_selections;
    std::string failure_phase;
    std::string failure_message;
    std::string failure_system;
};

struct FrameInput {
    IntentTime now = 0;
    std::map<ComponentTypeId, std::map<ComponentName, ComponentSlotId>> resolutions;
    std::vector<ResolvedEffect> resolvedEffects;
};

struct FrameResult {
    FrameLog frame;
    std::vector<EffectCommand> commands;
    std::vector<EffectReport> reports;
    std::vector<ExternalObservation> observations;
};

enum class ComponentControl {
    SelectionOnly,
    InternalState,
    ExternalEffect
};

struct RuntimeOptions {
    SessionId sessionId;
    FeedbackTiming feedbackTiming = FeedbackTiming::Deferred;
    RetryPolicy retryPolicy;
    std::size_t feedbackCapacity = 1024;
    std::size_t maxRetainedCommands = 4096;
    EventStore* eventStore = nullptr;
    bool allowVolatileEffects = false;
};

namespace detail {
class RuntimeEffectsState;
}

class Runtime {
private:
    struct ExternalComponentBinding {
        ComponentTarget component;
        ComponentName name;
        AdapterRoute route;
        EffectTarget target;
    };

    World ownedWorld;
    FrameNumber currentFrame = 0;
    FrameLog latestFrameLog;
    bool frameInProgress = false;
    bool faultedState = false;
    std::thread::id ownerThread = std::this_thread::get_id();
    std::unique_ptr<detail::RuntimeEffectsState> effectsState;
    std::map<ComponentTarget, ComponentControl> componentControls;
    std::map<ComponentTarget, ExternalComponentBinding> effectBindings;
    std::map<std::pair<std::string, std::string>, ComponentTarget>
        componentsByEffectTarget;

    void ensure_owner_thread() const;
    void validate_frame_start(
        IntentTime now,
        const std::map<ComponentTypeId,
            std::map<ComponentName, ComponentSlotId>>& resolutions
    ) const;
    FrameLog execute_frame_phases(
        IntentTime now,
        const std::map<ComponentTypeId,
            std::map<ComponentName, ComponentSlotId>>& resolutions
    );
    void record_world_evidence();
    std::vector<ResolvedEffect> apply_selections(const FrameLog& frame);
    void project_authoritative_reports(
        const std::vector<EffectReport>& reports);
    void project_authoritative_observations(
        const std::vector<ExternalObservation>& observations);

public:
    Runtime();
    explicit Runtime(FrameNumber initialFrame);
    explicit Runtime(RuntimeOptions options);
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;

    World& world();
    const World& world() const;
    FrameNumber frame() const;
    bool faulted() const;
    FrameLog run_frame(
        IntentTime now,
        std::map<ComponentTypeId, std::map<ComponentName, ComponentSlotId>> resolutions = {}
    );
    FrameResult run_frame(FrameInput input);
    void register_adapter(std::shared_ptr<EffectAdapter> adapter);
    template <typename Component>
    void configure_component(
        ComponentType<Component> type,
        const ComponentName& name,
        ComponentControl control);

    template <typename Component>
    void bind_effect_component(
        ComponentType<Component> type,
        const ComponentName& name,
        EffectTarget target);
#ifdef LIQUID_ENABLE_LEGACY_INTERNAL_COMPONENT_REGISTRATION
    void register_adapter(EffectAdapter& adapter) {
        register_adapter(std::shared_ptr<EffectAdapter>(
            &adapter, [](EffectAdapter*) {}));
    }
#endif
    FeedbackSender feedback_sender() const;
    std::optional<Value> observed_state(
        const AdapterRoute& route,
        const EffectTarget& target
    ) const;
    std::optional<CommandStatus> command_status(
        CommandId commandId
    ) const;
    void reconcile_indeterminate(
        const AdapterRoute& route,
        const EffectTarget& target,
        Value observedValue
    );
    RecordId checkpoint();
    const FrameLog& last_frame_log() const;
};

template <typename Component>
void Runtime::configure_component(
    ComponentType<Component> type,
    const ComponentName& name,
    ComponentControl control
) {
    ensure_owner_thread();
    if (!effectsState)
        throw std::logic_error(
            "component controls require an effects-configured runtime");
    if (frameInProgress)
        throw std::logic_error(
            "component control cannot change during a frame");
    if (control == ComponentControl::ExternalEffect)
        throw std::invalid_argument(
            "external components require bind_effect_component");
    const ComponentTarget target = ownedWorld.component_target(type, name);
    componentControls.insert_or_assign(target, control);
    effectBindings.erase(target);
}

template <typename Component>
void Runtime::bind_effect_component(
    ComponentType<Component> type,
    const ComponentName& name,
    EffectTarget target
) {
    ensure_owner_thread();
    if (!effectsState)
        throw std::logic_error(
            "effect bindings require an effects-configured runtime");
    if (frameInProgress)
        throw std::logic_error(
            "effect binding cannot change during a frame");
    const ComponentTarget component = ownedWorld.component_target(type, name);
    const AdapterRoute route = ownedWorld.effect_route(type.id);
    const std::pair<std::string, std::string> key{
        route.value(), target.value()};
    const auto existing = componentsByEffectTarget.find(key);
    if (existing != componentsByEffectTarget.end() &&
        existing->second != component) {
        throw std::invalid_argument(
            "effect route and target are already bound");
    }
    componentControls.insert_or_assign(
        component, ComponentControl::ExternalEffect);
    effectBindings.insert_or_assign(component, ExternalComponentBinding{
        component, name, route, std::move(target)});
    componentsByEffectTarget.insert_or_assign(key, component);
}

}
