#include "liquid/events/EventStore.hpp"

#include "EventInternals.hpp"

#include <utility>

namespace liquid {

RecordId EventStore::checkpoint(Value serializedProjection, Durability durability) {
    events_detail::validate_checkpoint_session(
        serializedProjection, metadata().session);
    return append(EventData{
        EventType::Checkpoint,
        1,
        std::move(serializedProjection)
    }, durability);
}

}
