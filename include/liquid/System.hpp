#pragma once

#include "liquid/Ids.hpp"
#include "liquid/IntentLifetime.hpp"

#include <set>

namespace liquid {

class World;
namespace detail { class SystemRegistry; }

class System {
private:
    std::set<BehaviorId> behaviours;

    friend class detail::SystemRegistry;

protected:
    const std::set<BehaviorId>& behaviors() const;

public:
    virtual ~System() = default;

    virtual void on_behavior_added(BehaviorId behavior);
    virtual void on_behavior_removed(BehaviorId behavior);
    virtual void run(World& world, FrameNumber frame, IntentTime now);
};

}
