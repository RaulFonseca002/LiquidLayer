#include "liquid/Ids.hpp"

#include <cassert>
#include <cstdint>
#include <limits>
#include <type_traits>

struct Light {
    int brightness = 0;
};

int main()
{
    static_assert(std::is_same_v<BehaviorId, std::uint16_t>);
    static_assert(std::is_same_v<IntentId, std::uint32_t>);
    static_assert(std::is_same_v<ComponentTypeId, std::uint16_t>);
    static_assert(std::is_same_v<ComponentSlotId, Slot>);

    assert(MaxBehaviours == MAX_BEHAVIOURS);
    assert(MaxIntents == MAX_INTENTS);
    assert(MaxComponentTypes == MAX_COMPONENT_TYPES);
    assert(MaxComponentSlots == MAX_COMPONENT_SLOTS);
    assert(MaxComponentTypes == 64);
    assert(InvalidComponentTypeId == std::numeric_limits<ComponentTypeId>::max());

    ComponentType<Light> invalidType;
    assert(invalidType.id == InvalidComponentTypeId);

    ComponentType<Light> lightType{7};
    ComponentTypeId converted = lightType;
    assert(converted == 7);

    Signature empty;
    assert(empty.none());
    assert(empty.count() == 0);

    Signature behaviorSignature;
    behaviorSignature.set(0);
    behaviorSignature.set(MaxComponentTypes - 1);

    assert(behaviorSignature.test(0));
    assert(behaviorSignature.test(MaxComponentTypes - 1));
    assert(behaviorSignature.count() == 2);

    Signature lightRequirement;
    lightRequirement.set(0);
    assert((behaviorSignature & lightRequirement) == lightRequirement);

    Signature missingRequirement;
    missingRequirement.set(1);
    assert((behaviorSignature & missingRequirement) != missingRequirement);

    behaviorSignature.reset(0);
    assert(!behaviorSignature.test(0));
    assert(behaviorSignature.count() == 1);

    behaviorSignature.flip(MaxComponentTypes - 1);
    assert(behaviorSignature.none());

    return 0;
}
