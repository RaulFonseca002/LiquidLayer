#include "liquid/Runtime.hpp"

#include <stdexcept>

World& Runtime::world() {
    return ownedWorld;
}

const World& Runtime::world() const {
    return ownedWorld;
}

FrameNumber Runtime::frame() const {
    return currentFrame;
}

bool Runtime::faulted() const {
    return faultedState;
}

FrameLog Runtime::run_frame(
    IntentTime now,
    std::map<ComponentTypeId, std::map<ComponentName, ComponentSlotId>> resolutions
) {
    if (faultedState)
        throw std::logic_error("runtime is faulted after an incomplete frame");

    if (frameInProgress)
        throw std::logic_error("runtime frame execution is not reentrant");

    if (currentFrame > 0 && now < latestFrameLog.now)
        throw std::invalid_argument("runtime time cannot move backwards");

    for (const auto& [type, components] : resolutions) {
        if (!ownedWorld.resolution_request_is_current(type, components))
            throw std::invalid_argument("intent resolution request contains a stale component target");
    }

    FrameLog log;
    log.frame = currentFrame;
    log.now = now;
    frameInProgress = true;

    try {
        log.phases.push_back("begin_frame");

        log.phases.push_back("expire_intents");
        log.expired_intents = ownedWorld.destroy_expired_intents(now);

        log.phases.push_back("run_systems");
        ownedWorld.run_systems(currentFrame, now, &log.systems_run);

        log.phases.push_back("resolve_intents");
        log.expired_intents += ownedWorld.destroy_expired_intents(now);
        log.resolution_requests = resolutions.size();

        for (const auto& [type, components] : resolutions) {
            std::map<ComponentName, IntentId> selected = ownedWorld.resolve_intents(type, components, now);
            log.selected_intents += selected.size();
            log.intent_selections.emplace(type, std::move(selected));
        }

        log.phases.push_back("end_frame");
        log.completed = true;
    } catch (const std::exception& error) {
        log.failure_phase = log.phases.empty() ? "begin_frame" : log.phases.back();
        log.failure_message = error.what();
        latestFrameLog = std::move(log);
        frameInProgress = false;
        faultedState = true;
        throw;
    } catch (...) {
        log.failure_phase = log.phases.empty() ? "begin_frame" : log.phases.back();
        log.failure_message = "unknown exception";
        latestFrameLog = std::move(log);
        frameInProgress = false;
        faultedState = true;
        throw;
    }

    latestFrameLog = std::move(log);
    frameInProgress = false;
    ++currentFrame;

    return latestFrameLog;
}

const FrameLog& Runtime::last_frame_log() const {
    return latestFrameLog;
}
