#include "liquid/Ids.hpp"
#include "liquid/world/World.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <type_traits>

using namespace liquid;

struct Light {
    int brightness = 0;
};

TEST_CASE("test_ids")
{
    static_assert(!std::is_integral_v<BehaviorId>);
    static_assert(!std::is_integral_v<IntentId>);
    static_assert(std::is_same_v<ComponentTypeId, std::uint16_t>);
    static_assert(!std::is_integral_v<ComponentSlotId>);

    REQUIRE(MaxBehaviours == MaxBehaviours);
    REQUIRE(MaxIntents == MaxIntents);
    REQUIRE(MaxComponentTypes == MaxComponentTypes);
    REQUIRE(MaxComponentSlots == MaxComponentSlots);
    REQUIRE(MaxComponentTypes == 64);
    REQUIRE(InvalidComponentTypeId == std::numeric_limits<ComponentTypeId>::max());

    ComponentType<Light> invalidType;
    REQUIRE(invalidType.id == InvalidComponentTypeId);

    ComponentType<Light> lightType{7};
    ComponentTypeId converted = lightType;
    REQUIRE(converted == 7);

    Signature empty;
    REQUIRE(empty.none());
    REQUIRE(empty.count() == 0);

    Signature behaviorSignature;
    behaviorSignature.set(0);
    behaviorSignature.set(MaxComponentTypes - 1);

    REQUIRE(behaviorSignature.test(0));
    REQUIRE(behaviorSignature.test(MaxComponentTypes - 1));
    REQUIRE(behaviorSignature.count() == 2);

    Signature lightRequirement;
    lightRequirement.set(0);
    REQUIRE((behaviorSignature & lightRequirement) == lightRequirement);

    Signature missingRequirement;
    missingRequirement.set(1);
    REQUIRE((behaviorSignature & missingRequirement) != missingRequirement);

    behaviorSignature.reset(0);
    REQUIRE(!behaviorSignature.test(0));
    REQUIRE(behaviorSignature.count() == 1);

    behaviorSignature.flip(MaxComponentTypes - 1);
    REQUIRE(behaviorSignature.none());

    World firstWorld;
    World secondWorld;
    auto firstLight = firstWorld.register_component<Light>("test.Light");
    REQUIRE(firstLight.world == firstWorld.instance_id());
    bool crossWorldRejected = false;
    try {
        secondWorld.add_component(firstLight, "foreign", Light{});
    } catch (const std::runtime_error&) {
        crossWorldRejected = true;
    }
    REQUIRE(crossWorldRejected);

    BehaviorId first = firstWorld.create_behavior();
    REQUIRE(first.world == firstWorld.instance_id());
    REQUIRE(first.generation != 0);
    REQUIRE(!secondWorld.behavior_exists(first));

    firstWorld.destroy_behavior(first);
    BehaviorId recycled = firstWorld.create_behavior();
    REQUIRE(recycled.slot == first.slot);
    REQUIRE(recycled.generation > first.generation);
    REQUIRE(!firstWorld.behavior_exists(first));
    REQUIRE(firstWorld.behavior_exists(recycled));

}
