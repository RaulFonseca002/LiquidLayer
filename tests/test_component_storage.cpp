#include "liquid/ComponentStorage.hpp"

#include <cassert>
#include <cstddef>
#include <memory>
#include <stdexcept>

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

    assert(thrown);
}

int main()
{
    {
        liquid::ComponentStorage<Light> storage;

        ComponentSlotId first = storage.add(Light{10});
        ComponentSlotId second = storage.add(Light{20});

        assert(first == 0);
        assert(second == 1);
        assert(storage.size() == 2);
        assert(storage.slot_count() == 2);
        assert(storage.has(first));
        assert(storage.has(second));
        assert(storage.get(first)->brightness == 10);
        assert(storage.get(second)->brightness == 20);

        const auto& constStorage = storage;
        assert(constStorage.get(first)->brightness == 10);
        assert(constStorage[first].brightness == 10);

        storage[first].brightness = 15;
        assert(storage.get(first)->brightness == 15);

        storage.remove(first);

        assert(!storage.has(first));
        assert(storage.has(second));
        assert(storage.get(first) == nullptr);
        assert(storage.size() == 1);
        assert(storage.slot_count() == 2);

        expect_throw([&] {
            storage[first];
        });

        expect_throw([&] {
            storage.remove(first);
        });

        ComponentSlotId reused = storage.add(Light{30});

        assert(reused == first);
        assert(storage.size() == 2);
        assert(storage.slot_count() == 2);
        assert(storage.get(reused)->brightness == 30);
    }

    {
        liquid::ComponentStorage<ThrowingConstruction> storage;

        ComponentSlotId slot = storage.add(ThrowingConstruction{10});
        storage.remove(slot);

        ThrowingConstruction::throwOnMoveConstruction = true;
        expect_throw([&] {
            storage.add(ThrowingConstruction{20});
        });
        ThrowingConstruction::throwOnMoveConstruction = false;

        assert(!storage.has(slot));
        assert(storage.get(slot) == nullptr);
        assert(storage.size() == 0);

        ComponentSlotId reused = storage.add(ThrowingConstruction{30});
        assert(reused == slot);
        assert(storage.get(reused)->value == 30);
        assert(storage.size() == 1);
    }

    {
        liquid::ComponentStorage<ResourceComponent> storage;
        std::shared_ptr<int> resource = std::make_shared<int>(42);
        std::weak_ptr<int> released = resource;
        ComponentSlotId slot = storage.add(ResourceComponent{resource});

        resource.reset();
        assert(!released.expired());

        storage.remove(slot);

        assert(released.expired());
        assert(!storage.has(slot));
    }

    {
        liquid::ComponentStorage<Light> storage;

        assert(!storage.has(42));
        assert(storage.get(42) == nullptr);

        expect_throw([&] {
            storage.remove(42);
        });

        expect_throw([&] {
            storage[42];
        });
    }

    {
        liquid::ComponentStorage<Light> storage;

        ComponentSlotId first = storage.add(Light{10});
        ComponentSlotId second = storage.add(Light{20});
        BehaviorId reader = 4;
        BehaviorId writer = 7;

        storage.addAccess(reader, liquid::ComponentSlotAccess::r, first);
        storage.addAccess(reader, liquid::ComponentSlotAccess::rw, second);
        storage.addAccess(writer, liquid::ComponentSlotAccess::w, first);

        expect_throw([&] {
            storage.addAccess(reader, liquid::ComponentSlotAccess::r, 42);
        });
        expect_throw([&] {
            storage.addAccess(
                reader,
                static_cast<liquid::ComponentSlotAccess::Mode>(3),
                first
            );
        });

        assert(storage.allAccesses().size() == 2);
        assert(storage.accessesOf(reader).size() == 2);
        assert(storage.accessesOf(reader)[0].slot == first);
        assert(storage.accessesOf(reader)[0].mode == liquid::ComponentSlotAccess::r);
        assert(storage.accessesOf(reader)[1].slot == second);
        assert(storage.accessesOf(reader)[1].mode == liquid::ComponentSlotAccess::rw);
        assert(storage.accessesOf(writer).size() == 1);
        assert(storage.accessesOf(writer)[0].mode == liquid::ComponentSlotAccess::w);

        expect_throw([&] {
            storage.accessesOf(99);
        });

        storage.removeAccess(reader, first);

        assert(storage.accessesOf(reader).size() == 1);
        assert(storage.accessesOf(reader)[0].slot == second);
        assert(storage.accessesOf(reader)[0].mode == liquid::ComponentSlotAccess::rw);

        storage.removeAccess(reader, first);
        assert(storage.accessesOf(reader).size() == 1);

        storage.remove(first);

        assert(!storage.has(first));
        assert(storage.accessesOf(reader).size() == 1);
        expect_throw([&] {
            storage.accessesOf(writer);
        });

        storage.removeAccessesOf(reader);
        assert(storage.allAccesses().empty());
    }

    {
        liquid::ComponentStorage<Light> storage;

        for (std::size_t i = 0; i < MAX_COMPONENT_SLOTS; ++i) {
            ComponentSlotId slot = storage.add(Light{static_cast<int>(i)});
            assert(slot == i);
            assert(storage.has(slot));
        }

        assert(storage.size() == MAX_COMPONENT_SLOTS);
        assert(storage.slot_count() == MAX_COMPONENT_SLOTS);

        expect_throw([&] {
            storage.add(Light{1});
        });
    }

    return 0;
}
