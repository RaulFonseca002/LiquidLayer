#include "liquid/detail/IntentRegistry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <set>
#include <stdexcept>

using namespace liquid;
using liquid::detail::IntentRegistry;

struct Light {
    int brightness = 0;
};

struct Temperature {
    int celsius = 0;
};

struct ThrowingIntentValue {
    static inline int movesUntilThrow = -1;

    int value = 0;

    explicit ThrowingIntentValue(int initialValue)
        : value(initialValue)
    {
    }

    ThrowingIntentValue(const ThrowingIntentValue&) = default;

    ThrowingIntentValue(ThrowingIntentValue&& other) {
        if (movesUntilThrow == 0)
            throw std::runtime_error("intent value move failed");

        if (movesUntilThrow > 0)
            --movesUntilThrow;

        value = other.value;
    }

    ThrowingIntentValue& operator=(const ThrowingIntentValue&) = default;
    ThrowingIntentValue& operator=(ThrowingIntentValue&&) = default;
};

template <typename Function>
void expect_throw(Function function)
{
    bool thrown = false;

    try {
        function();
    } catch (...) {
        thrown = true;
    }

    REQUIRE(thrown);
}

TEST_CASE("test_intent_registry")
{
    {
        IntentRegistry intents;

        BehaviorId owner = 7;
        ComponentType<Light> lightType{2};
        ComponentSlotId slot = 4;
        intents.create_behavior_pool(owner);

        IntentId id = intents.create(owner, lightType, slot, IntentLifetime::persistent(), Light{60});

        REQUIRE(intents.exists(id));
        REQUIRE(intents.owner_of(id) == owner);
        REQUIRE(intents.target_of(id) == (ComponentTarget{lightType.id, slot}));
        REQUIRE(intents.lifetime_of(id).kind == IntentLifetimeKind::Persistent);
        REQUIRE(intents.intent(id).id == id);
        REQUIRE(intents.intent(id).owner == owner);
        REQUIRE(intents.typed_intent(lightType, id).value.brightness == 60);
        REQUIRE(intents.intents_for(lightType.id, slot).size() == 1);
        REQUIRE(intents.target_index().at(lightType.id).at(slot).contains(id));
        REQUIRE(intents.size(owner) == 1);
        REQUIRE(!intents.exists(id + 100));
    }

    {
        IntentRegistry intents;
        BehaviorId owner = 1;
        ComponentType<Light> lightType{0};
        ComponentSlotId slot = 2;
        intents.create_behavior_pool(owner);

        IntentId first = intents.create(owner, lightType, slot, IntentLifetime::persistent(), Light{10});
        IntentId second = intents.create(owner, lightType, slot, IntentLifetime::persistent(), Light{20});
        intents.destroy(first);
        IntentId recycled = intents.create(owner, lightType, slot, IntentLifetime::persistent(), Light{30});

        REQUIRE(recycled.slot == first.slot);
        REQUIRE(recycled.generation > first.generation);
        REQUIRE(intents.intent(recycled).sequence > intents.intent(second).sequence);

        auto selected = intents.select(lightType.id, {{"light", slot}});
        REQUIRE(selected.at("light") == recycled);
    }

    {
        IntentRegistry intents;

        BehaviorId owner = 7;
        ComponentType<Light> lightType{2};
        ComponentSlotId slot = 4;
        intents.create_behavior_pool(owner);

        expect_throw([&] {
            intents.create(
                owner,
                lightType,
                slot,
                IntentLifetime{static_cast<IntentLifetimeKind>(3), 0},
                Light{10}
            );
        });
        expect_throw([&] {
            intents.create(
                owner,
                lightType,
                slot,
                IntentLifetime{IntentLifetimeKind::Persistent, 10},
                Light{10}
            );
        });
        expect_throw([&] {
            intents.create(
                owner,
                lightType,
                slot,
                IntentLifetime::persistent(),
                Light{10},
                static_cast<IntentPriority>(0)
            );
        });

        REQUIRE(intents.size() == 0);
        REQUIRE(intents.size(owner) == 0);
        REQUIRE(intents.target_index().empty());

        IntentId first = intents.create(
            owner,
            lightType,
            slot,
            IntentLifetime::persistent(),
            Light{20}
        );
        REQUIRE(first.slot == 1);
    }

    {
        IntentRegistry intents;

        BehaviorId owner = 7;
        ComponentType<ThrowingIntentValue> valueType{2};
        ComponentSlotId slot = 4;
        intents.create_behavior_pool(owner);

        for (int movesBeforeFailure : {0, 1}) {
            ThrowingIntentValue::movesUntilThrow = movesBeforeFailure;
            expect_throw([&] {
                intents.create(
                    owner,
                    valueType,
                    slot,
                    IntentLifetime::persistent(),
                    ThrowingIntentValue{10}
                );
            });

            REQUIRE(intents.size() == 0);
            REQUIRE(intents.size(owner) == 0);
            REQUIRE(intents.intents_for(valueType.id, slot).empty());
            REQUIRE(intents.target_index().empty());
        }

        ThrowingIntentValue::movesUntilThrow = -1;
        IntentId first = intents.create(
            owner,
            valueType,
            slot,
            IntentLifetime::persistent(),
            ThrowingIntentValue{20}
        );

        REQUIRE(first.slot == 1);
        REQUIRE(intents.typed_intent(valueType, first).value.value == 20);
    }

    {
        IntentRegistry intents;

        BehaviorId owner = 1;
        ComponentType<Light> lightType{0};
        ComponentSlotId slot = 2;
        intents.create_behavior_pool(owner);

        IntentId id = intents.create(owner, lightType, slot, IntentLifetime::persistent(), Light{10});
        intents.destroy(id);

        REQUIRE(!intents.exists(id));
        REQUIRE(intents.size(owner) == 0);
        REQUIRE(intents.intents_for(lightType.id, slot).empty());

        IntentId recycled = intents.create(owner, lightType, slot, IntentLifetime::persistent(), Light{20});
        REQUIRE(recycled.slot == id.slot);
        REQUIRE(recycled.generation > id.generation);
        REQUIRE(intents.exists(recycled));
        REQUIRE(intents.typed_intent(lightType, recycled).value.brightness == 20);
    }

    {
        IntentRegistry intents;

        BehaviorId owner = 3;
        ComponentType<Temperature> temperatureType{1};
        ComponentSlotId slot = 6;
        intents.create_behavior_pool(owner);

        IntentId beforeReset = intents.create(owner, temperatureType, slot, IntentLifetime::until_time(30), Temperature{22});
        REQUIRE(intents.exists(beforeReset));

        intents.create_behavior_pool(owner);

        REQUIRE(!intents.exists(beforeReset));
        REQUIRE(intents.size(owner) == 0);
        REQUIRE(intents.intents_for(temperatureType.id, slot).empty());

        IntentId afterReset = intents.create(owner, temperatureType, slot, IntentLifetime::until_time(30), Temperature{24});
        REQUIRE(afterReset.slot == beforeReset.slot);
        REQUIRE(afterReset.generation > beforeReset.generation);
        REQUIRE(intents.exists(afterReset));
    }

    {
        IntentRegistry intents;

        BehaviorId ownerOne = 1;
        BehaviorId ownerTwo = 2;
        ComponentType<Light> lightType{0};
        ComponentSlotId slot = 9;
        intents.create_behavior_pool(ownerOne);
        intents.create_behavior_pool(ownerTwo);

        IntentId ownedByOne = intents.create(ownerOne, lightType, slot, IntentLifetime::persistent(), Light{1});
        IntentId alsoOwnedByOne = intents.create(ownerOne, lightType, slot, IntentLifetime::persistent(), Light{2});
        IntentId ownedByTwo = intents.create(ownerTwo, lightType, slot, IntentLifetime::persistent(), Light{3});

        intents.destroy_owned_by(ownerOne);

        REQUIRE(!intents.exists(ownedByOne));
        REQUIRE(!intents.exists(alsoOwnedByOne));
        REQUIRE(intents.exists(ownedByTwo));
        REQUIRE(intents.size(ownerOne) == 0);
        REQUIRE(intents.size(ownerTwo) == 1);
        REQUIRE(intents.intents_for(lightType.id, slot).size() == 1);

        expect_throw([&] {
            intents.create(ownerOne, lightType, slot, IntentLifetime::persistent(), Light{4});
        });

        intents.create_behavior_pool(ownerOne);
        IntentId recreated = intents.create(ownerOne, lightType, slot, IntentLifetime::persistent(), Light{5});

        REQUIRE(intents.owner_of(recreated) == ownerOne);
        REQUIRE(intents.exists(recreated));
    }

    {
        IntentRegistry intents;

        IntentId missing = 99;
        REQUIRE(!intents.exists(missing));
        expect_throw([&] {
            intents.owner_of(missing);
        });
        REQUIRE(intents.size(99) == 0);
    }

    {
        IntentRegistry intents;
        BehaviorId owner = 11;
        ComponentType<Light> lightType{0};
        ComponentSlotId slot = 1;
        intents.create_behavior_pool(owner);
        std::set<IntentId> created;

        for (std::size_t i = 0; i < MaxIntents; ++i) {
            IntentId id = intents.create(owner, lightType, slot, IntentLifetime::persistent(), Light{static_cast<int>(i)});
            bool inserted = created.insert(id).second;
            REQUIRE(inserted);
            REQUIRE(intents.exists(id));
            REQUIRE(intents.owner_of(id) == owner);
        }

        REQUIRE(intents.size(owner) == MaxIntents);
        REQUIRE(intents.size() == MaxIntents);

        expect_throw([&] {
            intents.create(owner, lightType, slot, IntentLifetime::persistent(), Light{0});
        });

        IntentId recycled = *created.begin();
        intents.destroy(recycled);
        REQUIRE(intents.size(owner) == MaxIntents - 1);
        REQUIRE(!intents.exists(recycled));
        IntentId recreated = intents.create(owner, lightType, slot, IntentLifetime::persistent(), Light{42});
        REQUIRE(recreated.slot == recycled.slot);
        REQUIRE(recreated.generation > recycled.generation);
        REQUIRE(intents.size(owner) == MaxIntents);
    }

    {
        IntentRegistry intents;
        BehaviorId owner = 12;
        ComponentType<Light> lightType{0};
        ComponentSlotId slot = 1;
        intents.create_behavior_pool(owner);

        IntentId named = intents.create(
            owner,
            lightType,
            slot,
            IntentLifetime::persistent(),
            Light{70},
            IntentPriority::High,
            Value{std::int64_t{70}},
            "evening-light");

        REQUIRE(intents.intent(named).name == "evening-light");
        REQUIRE(intents.intent_named(owner, "evening-light") == named);
        REQUIRE(intents.intents_owned_by(owner) == std::vector<IntentId>{named});
        expect_throw([&] {
            (void)intents.create(
                owner,
                lightType,
                slot,
                IntentLifetime::persistent(),
                Light{30},
                IntentPriority::Medium,
                Value{std::int64_t{30}},
                "evening-light");
        });

        intents.destroy(named);
        REQUIRE(!intents.intent_named(owner, "evening-light").has_value());
        IntentId replacement = intents.create(
            owner,
            lightType,
            slot,
            IntentLifetime::persistent(),
            Light{30},
            IntentPriority::Medium,
            Value{std::int64_t{30}},
            "evening-light");
        REQUIRE(intents.intent_named(owner, "evening-light") == replacement);
    }

}
