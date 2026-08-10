#include "liquid/BehaviorRegistry.hpp"

#include "liquid/Ids.hpp"

#include <cassert>
#include <cstddef>
#include <set>

template <typename Function>
void expect_throw(Function function)
{
    bool thrown = false;

    try {
        function();
    } catch (...) {
        thrown = true;
    }

    assert(thrown);
}

int main()
{
    {
        BehaviorRegistry behaviors;

        BehaviorId first = behaviors.create();
        BehaviorId second = behaviors.create();
        BehaviorId third = behaviors.create();

        assert(first == 0);
        assert(second == 1);
        assert(third == 2);
        assert(behaviors.exists(first));
        assert(behaviors.exists(second));
        assert(behaviors.exists(third));
        assert(behaviors.size() == 3);
    }

    {
        BehaviorRegistry behaviors;

        BehaviorId id = behaviors.create();
        behaviors.destroy(id);

        assert(!behaviors.exists(id));
        assert(behaviors.size() == 0);

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
        assert(recycledSecond == second);

        behaviors.destroy(first);
        behaviors.destroy(third);

        BehaviorId recycledThird = behaviors.create();
        BehaviorId recycledFirst = behaviors.create();
        assert(recycledThird == third);
        assert(recycledFirst == first);
        assert(behaviors.size() == 3);
    }

    {
        BehaviorRegistry behaviors;

        expect_throw([&] {
            behaviors.destroy(42);
        });

        assert(!behaviors.exists(42));
        assert(behaviors.size() == 0);
    }

    {
        BehaviorRegistry behaviors;
        std::set<BehaviorId> created;

        for (std::size_t i = 0; i < MAX_BEHAVIOURS; ++i) {
            BehaviorId id = behaviors.create();
            bool inserted = created.insert(id).second;
            assert(inserted);
            assert(behaviors.exists(id));
        }

        assert(behaviors.size() == MAX_BEHAVIOURS);
        assert(created.size() == MAX_BEHAVIOURS);

        expect_throw([&] {
            behaviors.create();
        });

        for (BehaviorId id : created)
            assert(behaviors.exists(id));
    }

    return 0;
}
