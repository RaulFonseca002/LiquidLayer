#include "liquid/detail/BehaviorRegistry.hpp"

#include "liquid/Ids.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <set>

using namespace liquid;
using liquid::detail::BehaviorRegistry;

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

TEST_CASE("test_behavior_registry")
{
    {
        BehaviorRegistry behaviors;

        BehaviorId first = behaviors.create();
        BehaviorId second = behaviors.create();
        BehaviorId third = behaviors.create();

        REQUIRE(first.slot == 0);
        REQUIRE(second.slot == 1);
        REQUIRE(third.slot == 2);
        REQUIRE(behaviors.exists(first));
        REQUIRE(behaviors.exists(second));
        REQUIRE(behaviors.exists(third));
        REQUIRE(behaviors.size() == 3);
    }

    {
        BehaviorRegistry behaviors;

        BehaviorId id = behaviors.create();
        behaviors.destroy(id);

        REQUIRE(!behaviors.exists(id));
        REQUIRE(behaviors.size() == 0);

        expect_throw([&] {
            behaviors.destroy(id);
        });
    }

    {
        BehaviorRegistry behaviors;

        BehaviorId first = behaviors.create();
        BehaviorId second = behaviors.create();
        BehaviorId third = behaviors.create();

        behaviors.destroy(second);
        BehaviorId recycledSecond = behaviors.create();
        REQUIRE(recycledSecond.slot == second.slot);
        REQUIRE(recycledSecond.generation > second.generation);

        behaviors.destroy(first);
        behaviors.destroy(third);

        BehaviorId recycledThird = behaviors.create();
        BehaviorId recycledFirst = behaviors.create();
        REQUIRE(recycledThird.slot == third.slot);
        REQUIRE(recycledThird.generation > third.generation);
        REQUIRE(recycledFirst.slot == first.slot);
        REQUIRE(recycledFirst.generation > first.generation);
        REQUIRE(behaviors.size() == 3);
    }

    {
        BehaviorRegistry behaviors;

        expect_throw([&] {
            behaviors.destroy(42);
        });

        REQUIRE(!behaviors.exists(42));
        REQUIRE(behaviors.size() == 0);
    }

    {
        BehaviorRegistry behaviors;
        std::set<BehaviorId> created;

        for (std::size_t i = 0; i < MaxBehaviours; ++i) {
            BehaviorId id = behaviors.create();
            bool inserted = created.insert(id).second;
            REQUIRE(inserted);
            REQUIRE(behaviors.exists(id));
        }

        REQUIRE(behaviors.size() == MaxBehaviours);
        REQUIRE(created.size() == MaxBehaviours);

        expect_throw([&] {
            behaviors.create();
        });

        for (BehaviorId id : created)
            REQUIRE(behaviors.exists(id));
    }

}
