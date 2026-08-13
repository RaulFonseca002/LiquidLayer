#include "liquid/detail/SystemRegistry.hpp"

#include <exception>

namespace liquid {

namespace {

void capture_first_exception(std::exception_ptr& firstException) {
    if (!firstException)
        firstException = std::current_exception();
}

}

const std::set<BehaviorId>& System::behaviors() const {
    return behaviours;
}

void System::on_behavior_added(BehaviorId behavior) {
    (void)behavior;
}

void System::on_behavior_removed(BehaviorId behavior) {
    (void)behavior;
}

void System::run(World& world, FrameNumber frame, IntentTime now) {
    (void)world;
    (void)frame;
    (void)now;
}

namespace detail {

SystemRegistry::DispatchGuard::DispatchGuard(SystemRegistry& owner)
    : registry(owner)
{
    ++registry.dispatchDepth;
}

SystemRegistry::DispatchGuard::~DispatchGuard() {
    --registry.dispatchDepth;
}

void SystemRegistry::ensure_structural_mutation_allowed() const {
    if (dispatchDepth > 0)
        throw std::logic_error("system registry topology cannot change during dispatch");
}

std::size_t SystemRegistry::size() const {
    return systems.size();
}

bool SystemRegistry::dispatching() const {
    return dispatchDepth > 0;
}

void SystemRegistry::update_behavior(BehaviorId behavior, Signature behaviorSignature) {
    ensure_structural_mutation_allowed();
    DispatchGuard guard(*this);
    std::exception_ptr firstException;

    for (const std::type_index& type : registrationOrder) {
        auto& record = systems.at(type);

        if ((behaviorSignature & record.signature) == record.signature) {
            auto [position, inserted] = record.system->behaviours.emplace(behavior);
            (void)position;

            if (inserted) {
                try {
                    record.system->on_behavior_added(behavior);
                } catch (...) {
                    capture_first_exception(firstException);
                }
            }
        } else {
            if (record.system->behaviours.erase(behavior) > 0) {
                try {
                    record.system->on_behavior_removed(behavior);
                } catch (...) {
                    capture_first_exception(firstException);
                }
            }
        }
    }

    if (firstException)
        std::rethrow_exception(firstException);
}

void SystemRegistry::remove_behavior(BehaviorId behavior) {
    ensure_structural_mutation_allowed();
    DispatchGuard guard(*this);
    std::exception_ptr firstException;

    for (const std::type_index& type : registrationOrder) {
        auto& record = systems.at(type);

        if (record.system->behaviours.erase(behavior) > 0) {
            try {
                record.system->on_behavior_removed(behavior);
            } catch (...) {
                capture_first_exception(firstException);
            }
        }
    }

    if (firstException)
        std::rethrow_exception(firstException);
}

std::size_t SystemRegistry::run_systems(
    World& world,
    FrameNumber frame,
    IntentTime now,
    std::size_t* completedSystems,
    std::string* failingSystem
) {
    ensure_structural_mutation_allowed();
    DispatchGuard guard(*this);
    std::size_t systemsRun = 0;

    if (completedSystems)
        *completedSystems = 0;

    for (const std::type_index& type : registrationOrder) {
        const auto& record = systems.at(type);
        if (failingSystem)
            *failingSystem = record.stableName + "@" + std::to_string(record.version);
        record.system->run(world, frame, now);
        ++systemsRun;

        if (completedSystems)
            *completedSystems = systemsRun;
    }

    if (failingSystem)
        failingSystem->clear();

    return systemsRun;
}

}

}
