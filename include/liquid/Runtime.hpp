#pragma once

#include "liquid/Ids.hpp"
#include "liquid/IntentLifetime.hpp"
#include "liquid/world/World.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

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
};

class Runtime {
private:
    World ownedWorld;
    FrameNumber currentFrame = 0;
    FrameLog latestFrameLog;
    bool frameInProgress = false;
    bool faultedState = false;

public:
    Runtime() = default;
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
    const FrameLog& last_frame_log() const;
};
