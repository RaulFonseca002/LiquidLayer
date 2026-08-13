#pragma once

#include "liquid/Ids.hpp"
#include "liquid/Value.hpp"
#include "liquid/effects/EffectTypes.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

namespace liquid {

using SchemaVersion = std::uint32_t;

struct ComponentSchema {
    TypeName name;
    SchemaVersion version = 0;

    bool operator==(const ComponentSchema& other) const = default;
};

template <typename Component>
struct ComponentCodec {
    std::function<Value(const Component&)> encode;
    std::function<Component(const Value&)> decode;
};

template <typename Component>
struct EffectCodec {
    AdapterRoute adapterRoute;
    std::function<std::optional<ResolvedEffect>(
        const ComponentName&, const Component&)> encode;
    std::function<Component(const Value&)> decodeObserved;
};

}
