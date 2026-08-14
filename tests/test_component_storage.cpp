#include "liquid/detail/ComponentStorage.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <stdexcept>

using namespace liquid;

struct Light {
    int brightness = 0;
};

struct ThrowingConstruction {
    static inline bool throwOnMoveConstruction = false;

    int value = 0;

    ThrowingConstruction() = default;
    explicit ThrowingConstruction(int initialValue)
        : value(initialValue)
    {
    }

    ThrowingConstruction(const ThrowingConstruction&) = default;

    ThrowingConstruction(ThrowingConstruction&& other) {
        if (throwOnMoveConstruction)
            throw std::runtime_error("move construction failed");

        value = other.value;
    }

    ThrowingConstruction& operator=(const ThrowingConstruction&) = default;
    ThrowingConstruction& operator=(ThrowingConstruction&&) = default;
};

struct ResourceComponent {
    std::shared_ptr<int> resource;
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

TEST_CASE("test_component_storage")
{
    {
        liquid::detail::ComponentStorage<Light> storage;

        ComponentSlotId first = storage.add(Light{10});
        ComponentSlotId second = storage.add(Light{20});

        REQUIRE(first.slot == 0);
        REQUIRE(second.slot == 1);
        REQUIRE(storage.size() == 2);
        REQUIRE(storage.slot_count() == 2);
        REQUIRE(storage.has(first));
        REQUIRE(storage.has(second));
        REQUIRE(storage.get(first)->brightness == 10);
        REQUIRE(storage.get(second)->brightness == 20);

        const auto& constStorage = storage;
        REQUIRE(constStorage.get(first)->brightness == 10);
        REQUIRE(constStorage[first].brightness == 10);

        storage[first].brightness = 15;
        REQUIRE(storage.get(first)->brightness == 15);

        storage.remove(first);

        REQUIRE(!storage.has(first));
        REQUIRE(storage.has(second));
        REQUIRE(storage.get(first) == nullptr);
        REQUIRE(storage.size() == 1);
        REQUIRE(storage.slot_count() == 2);

        expect_throw([&] {
            storage[first];
        });

        expect_throw([&] {
            storage.remove(first);
        });

        ComponentSlotId reused = storage.add(Light{30});

        REQUIRE(reused == first);
        REQUIRE(storage.size() == 2);
        REQUIRE(storage.slot_count() == 2);
        REQUIRE(storage.get(reused)->brightness == 30);
    }

    {
        liquid::detail::ComponentStorage<ThrowingConstruction> storage;

        ComponentSlotId slot = storage.add(ThrowingConstruction{10});
        storage.remove(slot);

        ThrowingConstruction::throwOnMoveConstruction = true;
        expect_throw([&] {
            storage.add(ThrowingConstruction{20});
        });
        ThrowingConstruction::throwOnMoveConstruction = false;

        REQUIRE(!storage.has(slot));
        REQUIRE(storage.get(slot) == nullptr);
        REQUIRE(storage.size() == 0);

        ComponentSlotId reused = storage.add(ThrowingConstruction{30});
        REQUIRE(reused == slot);
        REQUIRE(storage.get(reused)->value == 30);
        REQUIRE(storage.size() == 1);
    }

    {
        liquid::detail::ComponentStorage<ResourceComponent> storage;
        std::shared_ptr<int> resource = std::make_shared<int>(42);
        std::weak_ptr<int> released = resource;
        ComponentSlotId slot = storage.add(ResourceComponent{resource});

        resource.reset();
        REQUIRE(!released.expired());

        storage.remove(slot);

        REQUIRE(released.expired());
        REQUIRE(!storage.has(slot));
    }

    {
        liquid::detail::ComponentStorage<Light> storage;

        REQUIRE(!storage.has(42));
        REQUIRE(storage.get(42) == nullptr);

        expect_throw([&] {
            storage.remove(42);
        });

        expect_throw([&] {
            storage[42];
        });
    }

    {
        liquid::detail::ComponentStorage<Light> storage;

        ComponentSlotId first = storage.add(Light{10});
        ComponentSlotId second = storage.add(Light{20});
        BehaviorId reader = 4;
        BehaviorId writer = 7;

        storage.addAccess(reader, liquid::detail::ComponentSlotAccess::r, first);
        storage.addAccess(reader, liquid::detail::ComponentSlotAccess::rw, second);
        storage.addAccess(writer, liquid::detail::ComponentSlotAccess::w, first);

        expect_throw([&] {
            storage.addAccess(reader, liquid::detail::ComponentSlotAccess::r, 42);
        });
        expect_throw([&] {
            storage.addAccess(
                reader,
                static_cast<liquid::detail::ComponentSlotAccess::Mode>(3),
                first
            );
        });

        REQUIRE(storage.allAccesses().size() == 2);
        REQUIRE(storage.accessesOf(reader).size() == 2);
        REQUIRE(storage.accessesOf(reader)[0].slot == first);
        REQUIRE(storage.accessesOf(reader)[0].mode == liquid::detail::ComponentSlotAccess::r);
        REQUIRE(storage.accessesOf(reader)[1].slot == second);
        REQUIRE(storage.accessesOf(reader)[1].mode == liquid::detail::ComponentSlotAccess::rw);
        REQUIRE(storage.accessesOf(writer).size() == 1);
        REQUIRE(storage.accessesOf(writer)[0].mode == liquid::detail::ComponentSlotAccess::w);

        expect_throw([&] {
            storage.accessesOf(99);
        });

        storage.removeAccess(reader, first);

        REQUIRE(storage.accessesOf(reader).size() == 1);
        REQUIRE(storage.accessesOf(reader)[0].slot == second);
        REQUIRE(storage.accessesOf(reader)[0].mode == liquid::detail::ComponentSlotAccess::rw);

        storage.removeAccess(reader, first);
        REQUIRE(storage.accessesOf(reader).size() == 1);

        storage.remove(first);

        REQUIRE(!storage.has(first));
        REQUIRE(storage.accessesOf(reader).size() == 1);
        expect_throw([&] {
            storage.accessesOf(writer);
        });

        storage.removeAccessesOf(reader);
        REQUIRE(storage.allAccesses().empty());
    }

    {
        liquid::detail::ComponentStorage<Light> storage;

        for (std::size_t i = 0; i < MaxComponentSlots; ++i) {
            ComponentSlotId slot = storage.add(Light{static_cast<int>(i)});
            REQUIRE(slot.slot == i);
            REQUIRE(storage.has(slot));
        }

        REQUIRE(storage.size() == MaxComponentSlots);
        REQUIRE(storage.slot_count() == MaxComponentSlots);

        expect_throw([&] {
            storage.add(Light{1});
        });
    }

}
