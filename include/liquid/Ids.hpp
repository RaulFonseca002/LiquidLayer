#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <compare>
#include <functional>

namespace liquid {

using BehaviorAccessRevision = std::uint64_t;
using WorldInstanceId = std::uint64_t;
using IntentSequence = std::uint64_t;
using FrameNumber = std::uint64_t;
using ComponentName = std::string;
using Slot = std::uint16_t;
using ComponentTypeId = std::uint16_t;
using TypeName = std::string;

template <typename Tag, typename SlotType>
struct GenerationalHandle {
    WorldInstanceId world = 0;
    SlotType slot = 0;
    std::uint32_t generation = 0;

    constexpr GenerationalHandle() = default;
    constexpr GenerationalHandle(SlotType legacySlot)
        : slot(legacySlot) {
    }
    constexpr GenerationalHandle(WorldInstanceId owningWorld, SlotType owningSlot, std::uint32_t owningGeneration)
        : world(owningWorld), slot(owningSlot), generation(owningGeneration) {
    }

    constexpr explicit operator bool() const { return generation != 0; }
    constexpr operator SlotType() const { return slot; }
    bool operator==(const GenerationalHandle&) const = default;
    auto operator<=>(const GenerationalHandle&) const = default;

};

struct BehaviorHandleTag;
struct IntentHandleTag;
struct ComponentSlotHandleTag;
using BehaviorId = GenerationalHandle<BehaviorHandleTag, std::uint16_t>;
using IntentId = GenerationalHandle<IntentHandleTag, std::uint32_t>;
using ComponentSlotId = GenerationalHandle<ComponentSlotHandleTag, Slot>;

inline constexpr ComponentTypeId InvalidComponentTypeId =
    std::numeric_limits<ComponentTypeId>::max();
inline constexpr ComponentSlotId InvalidComponentSlotId{};

struct ComponentTarget {
    ComponentTypeId type = InvalidComponentTypeId;
    ComponentSlotId slot = InvalidComponentSlotId;

    bool operator<(const ComponentTarget& other) const {
        if (type != other.type)
            return type < other.type;

        return slot < other.slot;
    }

    bool operator==(const ComponentTarget& other) const {
        return type == other.type && slot == other.slot;
    }
};

// Typed runtime handle for a registered component type.
// The explicit TypeName creates the ComponentTypeId during registration; after
// that, outside code passes this handle back instead of depending on typeid or
// hidden global template state. The id is world-local and recyclable later.
template <typename Component>
struct ComponentType {
    ComponentTypeId id = InvalidComponentTypeId;
    WorldInstanceId world = 0;
    std::uint32_t generation = 0;

    operator ComponentTypeId() const {
        return id;
    }

    bool operator==(const ComponentType&) const = default;
};

enum class ComponentAccessMode {
    Read,
    Write,
    ReadWrite
};

enum class IntentPriority : std::uint8_t {
    Low = 55,
    Medium = 155,
    High = 255
};

inline constexpr std::size_t MaxBehaviours = 5000;
inline constexpr std::size_t MaxIntents = 5000;
inline constexpr std::size_t MaxComponentTypes = 64;
inline constexpr std::size_t MaxComponentSlots =
    static_cast<std::size_t>(std::numeric_limits<Slot>::max());

using Signature = std::bitset<MaxComponentTypes>;

}
namespace std {

template <typename Tag, typename SlotType>
struct hash<liquid::GenerationalHandle<Tag, SlotType>> {
    std::size_t operator()(const liquid::GenerationalHandle<Tag, SlotType>& value) const {
        std::size_t result = std::hash<liquid::WorldInstanceId>{}(value.world);
        result ^= std::hash<SlotType>{}(value.slot) + 0x9e3779b9U + (result << 6U) + (result >> 2U);
        result ^= std::hash<std::uint32_t>{}(value.generation) + 0x9e3779b9U + (result << 6U) + (result >> 2U);
        return result;
    }
};

}
