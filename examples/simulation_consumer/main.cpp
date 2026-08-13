#include <liquid/Runtime.hpp>
#include <liquid/simulation/InMemoryAdapter.hpp>

#include <cstdint>
#include <memory>

int main() {
    liquid::RuntimeOptions options;
    options.sessionId = liquid::SessionId{9};
    options.feedbackTiming = liquid::FeedbackTiming::Immediate;
    options.allowVolatileEffects = true;
    liquid::Runtime runtime{options};

    auto adapter = std::make_shared<liquid::simulation::InMemoryAdapter>(
        liquid::AdapterRoute{"consumer.simulation"});
    runtime.register_adapter(adapter);

    const liquid::ResolvedEffect effect{
        adapter->route(),
        liquid::EffectTarget{"office"},
        liquid::Value{std::uint64_t{70}}
    };
    const liquid::FrameResult frame = runtime.run_frame(
        liquid::FrameInput{0, {}, {effect}}
    );

    return frame.commands.size() == 1 &&
        runtime.observed_state(adapter->route(), effect.target) == effect.desiredValue
        ? 0
        : 1;
}
