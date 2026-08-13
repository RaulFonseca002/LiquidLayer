#include <liquid/Runtime.hpp>
#include <liquid/events/MemoryEventStore.hpp>

int main() {
    const liquid::SessionId session{7};

    liquid::EventStoreMetadata metadata;
    metadata.session = session;
    metadata.engineVersion = "0.1.0-consumer";
    metadata.feedbackTiming = liquid::FeedbackTiming::Deferred;
    liquid::MemoryEventStore store{metadata};

    liquid::RuntimeOptions options;
    options.sessionId = session;
    options.eventStore = &store;
    liquid::Runtime runtime{options};

    const liquid::BehaviorId behavior = runtime.world().create_behavior();
    const liquid::FrameResult frame = runtime.run_frame(
        liquid::FrameInput{0, {}, {}}
    );

    return behavior && frame.frame.completed && !store.read_all().empty() ? 0 : 1;
}
