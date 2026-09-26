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

namespace {

std::set<IntentId> as_set(const std::vector<IntentId>& ids)
{
    return {ids.begin(), ids.end()};
}

}

TEST_CASE("intent transaction commit keeps cancellations and creations")
{
    IntentRegistry intents;
    BehaviorId owner = 7;
    ComponentType<Light> lightType{2};
    ComponentSlotId slot = 4;
    intents.create_behavior_pool(owner);

    IntentId named = intents.create(
        owner, lightType, slot, IntentLifetime::persistent(), Light{10},
        IntentPriority::Medium, Value{}, "a");
    IntentId unnamed = intents.create(
        owner, lightType, slot, IntentLifetime::persistent(), Light{20});
    const std::size_t recordsBefore = intents.lifecycle_records().size();

    auto tx = intents.begin_transaction(1);
    intents.cancel(*tx, named);
    IntentId created = intents.create(
        *tx, owner, lightType, slot, IntentLifetime::persistent(), Light{30},
        IntentPriority::High, Value{}, "b");
    intents.commit(*tx);

    REQUIRE(!intents.exists(named));
    REQUIRE(!intents.intent_named(owner, "a").has_value());
    REQUIRE(intents.intent_named(owner, "b") == created);
    REQUIRE(intents.exists(created));
    REQUIRE(intents.exists(unnamed));
    REQUIRE(intents.size(owner) == 2);
    REQUIRE(as_set(intents.intents_for(lightType.id, slot)) == std::set<IntentId>{unnamed, created});
    REQUIRE(as_set(intents.intents_owned_by(owner)) == std::set<IntentId>{unnamed, created});

    const auto& records = intents.lifecycle_records();
    REQUIRE(records.size() == recordsBefore + 2);
    bool sawCancel = false;
    bool sawCreate = false;
    for (std::size_t i = recordsBefore; i < records.size(); ++i) {
        if (records[i].intent.id == named && !records[i].created)
            sawCancel = true;
        if (records[i].intent.id == created && records[i].created)
            sawCreate = true;
    }
    REQUIRE(sawCancel);
    REQUIRE(sawCreate);

    IntentId after = intents.create(
        owner, lightType, slot, IntentLifetime::persistent(), Light{40});
    REQUIRE(intents.exists(after));
    intents.destroy(after);
    REQUIRE(!intents.exists(after));
}

TEST_CASE("intent transaction rollback restores cancelled intents and drops creations")
{
    IntentRegistry intents;
    BehaviorId owner = 7;
    ComponentType<Light> lightType{2};
    ComponentSlotId slot = 4;
    intents.create_behavior_pool(owner);

    IntentId x = intents.create(
        owner, lightType, slot, IntentLifetime::until_time(90), Light{10},
        IntentPriority::Low, Value{}, "x");
    IntentId y = intents.create(
        owner, lightType, slot, IntentLifetime::persistent(), Light{20});
    const auto liveBefore = intents.live_intent_ids();
    const std::size_t recordsBefore = intents.lifecycle_records().size();
    const BehaviorId ownerBefore = intents.owner_of(x);
    const ComponentTarget targetBefore = intents.target_of(x);
    const IntentLifetime lifetimeBefore = intents.lifetime_of(x);

    auto tx = intents.begin_transaction(1);
    intents.cancel(*tx, x);
    IntentId z = intents.create(
        *tx, owner, lightType, slot, IntentLifetime::persistent(), Light{30},
        IntentPriority::Medium, Value{}, "z");
    REQUIRE(!intents.exists(x));
    REQUIRE(intents.exists(z));
    intents.rollback(*tx);

    REQUIRE(intents.exists(x));
    REQUIRE(intents.exists(y));
    REQUIRE(intents.owner_of(x) == ownerBefore);
    REQUIRE(intents.target_of(x) == targetBefore);
    REQUIRE(intents.lifetime_of(x).kind == lifetimeBefore.kind);
    REQUIRE(intents.lifetime_of(x).expiresAt == lifetimeBefore.expiresAt);
    REQUIRE(intents.typed_intent(lightType, x).value.brightness == 10);
    REQUIRE(intents.intent_named(owner, "x") == x);
    REQUIRE(!intents.exists(z));
    REQUIRE(!intents.intent_named(owner, "z").has_value());
    REQUIRE(intents.live_intent_ids() == liveBefore);
    REQUIRE(intents.lifecycle_records().size() == recordsBefore);
    REQUIRE(intents.size(owner) == 2);

    IntentId after = intents.create(
        owner, lightType, slot, IntentLifetime::persistent(), Light{40});
    REQUIRE(intents.exists(after));
    REQUIRE(after != z);
    REQUIRE(!intents.exists(z));
}

TEST_CASE("intent transaction destructor rolls back when not committed")
{
    IntentRegistry intents;
    BehaviorId owner = 7;
    ComponentType<Light> lightType{2};
    ComponentSlotId slot = 4;
    intents.create_behavior_pool(owner);

    IntentId x = intents.create(
        owner, lightType, slot, IntentLifetime::persistent(), Light{10});
    IntentId z;
    {
        auto tx = intents.begin_transaction(1);
        intents.cancel(*tx, x);
        z = intents.create(
            *tx, owner, lightType, slot, IntentLifetime::persistent(), Light{30});
        REQUIRE(intents.exists(z));
    }

    REQUIRE(intents.exists(x));
    REQUIRE(!intents.exists(z));
    REQUIRE(intents.size(owner) == 1);

    IntentId after = intents.create(
        owner, lightType, slot, IntentLifetime::persistent(), Light{40});
    REQUIRE(intents.exists(after));
}

TEST_CASE("intent registry rejects ordinary mutation during a transaction")
{
    IntentRegistry intents;
    BehaviorId owner = 7;
    ComponentType<Light> lightType{2};
    ComponentSlotId slot = 4;
    intents.create_behavior_pool(owner);

    IntentId x = intents.create(
        owner, lightType, slot, IntentLifetime::persistent(), Light{10});

    auto tx = intents.begin_transaction(0);
    REQUIRE_THROWS_AS(
        intents.create(owner, lightType, slot, IntentLifetime::persistent(), Light{}),
        std::logic_error);
    REQUIRE_THROWS_AS(intents.destroy(x), std::logic_error);
    REQUIRE_THROWS_AS(intents.destroy_owned_by(owner), std::logic_error);
    REQUIRE_THROWS_AS(intents.begin_transaction(0), std::logic_error);
    REQUIRE(intents.exists(x));
    REQUIRE(intents.size(owner) == 1);

    intents.commit(*tx);
    intents.destroy(x);
    REQUIRE(!intents.exists(x));
}

TEST_CASE("intent registry rejects operations on a finished or foreign transaction")
{
    IntentRegistry first{1};
    IntentRegistry second{2};
    BehaviorId owner = 7;
    ComponentType<Light> lightType{2};
    ComponentSlotId slot = 4;
    first.create_behavior_pool(owner);
    second.create_behavior_pool(owner);

    IntentId x = first.create(
        owner, lightType, slot, IntentLifetime::persistent(), Light{10});

    auto tx = first.begin_transaction(1);
    first.commit(*tx);
    REQUIRE_THROWS_AS(first.cancel(*tx, x), std::logic_error);
    REQUIRE_THROWS_AS(
        first.create(*tx, owner, lightType, slot, IntentLifetime::persistent(), Light{}),
        std::logic_error);
    REQUIRE_THROWS_AS(first.commit(*tx), std::logic_error);

    auto foreign = second.begin_transaction(1);
    REQUIRE_THROWS_AS(first.cancel(*foreign, x), std::logic_error);
    REQUIRE_THROWS_AS(
        first.create(*foreign, owner, lightType, slot, IntentLifetime::persistent(), Light{}),
        std::logic_error);
    second.commit(*foreign);

    REQUIRE(first.exists(x));
    REQUIRE(second.size(owner) == 0);
}

TEST_CASE("intent transaction cancel validates the intent id")
{
    IntentRegistry intents{1};
    IntentRegistry other{2};
    BehaviorId owner = 7;
    ComponentType<Light> lightType{2};
    ComponentSlotId slot = 4;
    intents.create_behavior_pool(owner);
    other.create_behavior_pool(owner);

    IntentId stale = intents.create(
        owner, lightType, slot, IntentLifetime::persistent(), Light{1});
    intents.destroy(stale);
    IntentId x = intents.create(
        owner, lightType, slot, IntentLifetime::persistent(), Light{10});
    IntentId foreign = other.create(
        owner, lightType, slot, IntentLifetime::persistent(), Light{20});

    auto tx = intents.begin_transaction(2);
    REQUIRE_THROWS_AS(intents.cancel(*tx, stale), std::runtime_error);
    REQUIRE_THROWS(intents.cancel(*tx, foreign));
    REQUIRE(intents.exists(x));
    intents.commit(*tx);

    REQUIRE(intents.exists(x));
    REQUIRE(other.exists(foreign));
    intents.destroy(x);
    REQUIRE(intents.size(owner) == 0);
}

TEST_CASE("intent registry queries for unknown owners and slots return empty")
{
    IntentRegistry intents;
    BehaviorId owner = 7;
    ComponentType<Light> lightType{2};
    ComponentSlotId slot = 4;
    intents.create_behavior_pool(owner);
    (void)intents.create(owner, lightType, slot, IntentLifetime::persistent(), Light{10});

    REQUIRE(intents.intents_owned_by(BehaviorId{99}).empty());
    REQUIRE(!intents.intent_named(BehaviorId{99}, "any").has_value());
    REQUIRE(intents.intents_for(2, ComponentSlotId{5}).empty());
    REQUIRE(intents.intents_for(3, ComponentSlotId{4}).empty());
    REQUIRE(intents.intents_for(2, slot).size() == 1);
}
