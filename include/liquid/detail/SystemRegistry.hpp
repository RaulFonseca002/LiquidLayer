#pragma once

#include "liquid/System.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace liquid::detail {

class SystemRegistry {
private:
    struct SystemRecord {
        Signature signature;
        std::shared_ptr<System> system;
        std::string stableName;
        std::uint32_t version = 0;
        SystemPhase phase = SystemPhase::Decision;
    };

    std::unordered_map<std::type_index, SystemRecord> systems;
    std::vector<std::type_index> registrationOrder;
    std::size_t dispatchDepth = 0;

    class DispatchGuard {
    private:
        SystemRegistry& registry;

    public:
        explicit DispatchGuard(SystemRegistry& owner);
        ~DispatchGuard();

        DispatchGuard(const DispatchGuard&) = delete;
        DispatchGuard& operator=(const DispatchGuard&) = delete;
    };

    void ensure_structural_mutation_allowed() const;

public:
    SystemRegistry() = default;
    SystemRegistry(const SystemRegistry&) = delete;
    SystemRegistry& operator=(const SystemRegistry&) = delete;
    SystemRegistry(SystemRegistry&&) = delete;
    SystemRegistry& operator=(SystemRegistry&&) = delete;

    template <typename SystemType, typename... Args>
    void register_system(Signature signature, SystemPhase phase, Args&&... args);

    template <typename SystemType, typename... Args>
    void register_system(Signature signature, Args&&... args) {
        register_system<SystemType>(
            signature, SystemPhase::Decision, std::forward<Args>(args)...);
    }

    template <typename SystemType>
    void destroy_system();

    template <typename SystemType>
    bool exists() const;

    std::size_t size() const;

    template <typename SystemType>
    void set_signature(Signature signature);

    template <typename SystemType>
    Signature signature() const;

    template <typename SystemType>
    SystemType& get_system();

    template <typename SystemType>
    const SystemType& get_system() const;

    template <typename SystemType>
    void add_behavior(BehaviorId behavior);

    template <typename SystemType>
    void remove_behavior(BehaviorId behavior);

    template <typename SystemType>
    bool has_behavior(BehaviorId behavior) const;

    template <typename SystemType>
    std::size_t behavior_count() const;

    void update_behavior(BehaviorId behavior, Signature behaviorSignature);
    void remove_behavior(BehaviorId behavior);
    bool dispatching() const;
    std::size_t run_systems(
        World& world,
        FrameNumber frame,
        IntentTime now,
        SystemPhase phase,
        std::size_t* completedSystems = nullptr,
        std::string* failingSystem = nullptr
    );
};

template <typename SystemType, typename... Args>
void SystemRegistry::register_system(
    Signature signature,
    SystemPhase phase,
    Args&&... args
) {
    static_assert(std::is_base_of_v<System, SystemType>, "registered systems must inherit from System");
    ensure_structural_mutation_allowed();

    std::type_index type = std::type_index(typeid(SystemType));

    if (systems.contains(type))
        throw std::runtime_error("system already registered");

    static_assert(requires {
        { SystemType::stableName } -> std::convertible_to<std::string_view>;
        { SystemType::version } -> std::convertible_to<std::uint32_t>;
    }, "registered systems must define stableName and version");
    const std::string stableName{SystemType::stableName};
    const std::uint32_t version = SystemType::version;
    if (stableName.empty() || version == 0)
        throw std::invalid_argument("system stable name and version are required");

    std::shared_ptr<System> system = std::make_shared<SystemType>(std::forward<Args>(args)...);

    registrationOrder.reserve(registrationOrder.size() + 1);
    auto [position, inserted] = systems.emplace(type, SystemRecord{
        signature, std::move(system), stableName, version, phase});
    (void)position;

    if (!inserted)
        throw std::runtime_error("system already registered");

    registrationOrder.push_back(type);
}

template <typename SystemType>
void SystemRegistry::destroy_system() {
    ensure_structural_mutation_allowed();
    auto found = systems.find(std::type_index(typeid(SystemType)));

    if (found == systems.end())
        throw std::runtime_error("system not registered");

    std::type_index type = std::type_index(typeid(SystemType));

    systems.erase(found);
    registrationOrder.erase(
        std::remove(registrationOrder.begin(), registrationOrder.end(), type),
        registrationOrder.end()
    );
}

template <typename SystemType>
bool SystemRegistry::exists() const {
    return systems.contains(std::type_index(typeid(SystemType)));
}

template <typename SystemType>
void SystemRegistry::set_signature(Signature signature) {
    ensure_structural_mutation_allowed();
    auto found = systems.find(std::type_index(typeid(SystemType)));

    if (found == systems.end())
        throw std::runtime_error("system not registered");

    found->second.signature = signature;
}

template <typename SystemType>
Signature SystemRegistry::signature() const {
    auto found = systems.find(std::type_index(typeid(SystemType)));

    if (found == systems.end())
        throw std::runtime_error("system not registered");

    return found->second.signature;
}

template <typename SystemType>
SystemType& SystemRegistry::get_system() {
    auto found = systems.find(std::type_index(typeid(SystemType)));

    if (found == systems.end())
        throw std::runtime_error("system not registered");

    auto system = std::dynamic_pointer_cast<SystemType>(found->second.system);

    if (!system)
        throw std::runtime_error("system type mismatch");

    return *system;
}

template <typename SystemType>
const SystemType& SystemRegistry::get_system() const {
    auto found = systems.find(std::type_index(typeid(SystemType)));

    if (found == systems.end())
        throw std::runtime_error("system not registered");

    auto system = std::dynamic_pointer_cast<const SystemType>(found->second.system);

    if (!system)
        throw std::runtime_error("system type mismatch");

    return *system;
}

template <typename SystemType>
void SystemRegistry::add_behavior(BehaviorId behavior) {
    ensure_structural_mutation_allowed();
    auto found = systems.find(std::type_index(typeid(SystemType)));

    if (found == systems.end())
        throw std::runtime_error("system not registered");

    auto [position, inserted] = found->second.system->behaviours.emplace(behavior);
    (void)position;

    if (inserted) {
        DispatchGuard guard(*this);
        found->second.system->on_behavior_added(behavior);
    }
}

template <typename SystemType>
void SystemRegistry::remove_behavior(BehaviorId behavior) {
    ensure_structural_mutation_allowed();
    auto found = systems.find(std::type_index(typeid(SystemType)));

    if (found == systems.end())
        throw std::runtime_error("system not registered");

    if (found->second.system->behaviours.erase(behavior) > 0) {
        DispatchGuard guard(*this);
        found->second.system->on_behavior_removed(behavior);
    }
}

template <typename SystemType>
bool SystemRegistry::has_behavior(BehaviorId behavior) const {
    auto found = systems.find(std::type_index(typeid(SystemType)));

    if (found == systems.end())
        throw std::runtime_error("system not registered");

    return found->second.system->behaviours.contains(behavior);
}

template <typename SystemType>
std::size_t SystemRegistry::behavior_count() const {
    auto found = systems.find(std::type_index(typeid(SystemType)));

    if (found == systems.end())
        throw std::runtime_error("system not registered");

    return found->second.system->behaviours.size();
}

}
