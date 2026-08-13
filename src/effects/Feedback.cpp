#include "liquid/effects/Feedback.hpp"

#include <deque>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace liquid::detail {

class FeedbackState {
public:
    explicit FeedbackState(std::size_t maximumReports) : capacity(maximumReports) {}

    mutable std::mutex mutex;
    std::deque<EffectReport> reports;
    std::size_t capacity;
    bool shutdown = false;
};

}

namespace liquid {

FeedbackSender::FeedbackSender(
    const std::shared_ptr<detail::FeedbackState>& sharedState
) : state(sharedState) {}

FeedbackSendResult FeedbackSender::try_send(EffectReport report) const {
    validate_effect_report(report);

    const auto sharedState = state.lock();
    if (!sharedState)
        return FeedbackSendResult::Closed;

    std::lock_guard lock(sharedState->mutex);
    if (sharedState->shutdown)
        return FeedbackSendResult::Closed;
    if (sharedState->reports.size() >= sharedState->capacity)
        return FeedbackSendResult::Full;

    sharedState->reports.push_back(std::move(report));
    return FeedbackSendResult::Sent;
}

bool FeedbackSender::is_closed() const {
    const auto sharedState = state.lock();
    if (!sharedState)
        return true;

    std::lock_guard lock(sharedState->mutex);
    return sharedState->shutdown;
}

FeedbackReceiver::FeedbackReceiver(
    std::shared_ptr<detail::FeedbackState> sharedState
) : state(std::move(sharedState)) {}

FeedbackReceiver::~FeedbackReceiver() {
    shutdown();
}

FeedbackReceiver& FeedbackReceiver::operator=(FeedbackReceiver&& other) noexcept {
    if (this == &other)
        return *this;

    shutdown();
    state = std::move(other.state);
    return *this;
}

std::optional<EffectReport> FeedbackReceiver::try_receive() {
    if (!state)
        return std::nullopt;

    std::lock_guard lock(state->mutex);
    if (state->reports.empty())
        return std::nullopt;

    EffectReport report = std::move(state->reports.front());
    state->reports.pop_front();
    return report;
}

std::vector<EffectReport> FeedbackReceiver::drain() {
    std::vector<EffectReport> drained;
    if (!state)
        return drained;

    std::deque<EffectReport> queued;
    {
        std::lock_guard lock(state->mutex);
        queued.swap(state->reports);
    }
    drained.reserve(queued.size());
    while (!queued.empty()) {
        drained.push_back(std::move(queued.front()));
        queued.pop_front();
    }
    return drained;
}

std::size_t FeedbackReceiver::pending() const {
    if (!state)
        return 0;

    std::lock_guard lock(state->mutex);
    return state->reports.size();
}

void FeedbackReceiver::shutdown() {
    if (!state)
        return;

    std::lock_guard lock(state->mutex);
    state->shutdown = true;
}

bool FeedbackReceiver::is_shutdown() const {
    if (!state)
        return true;

    std::lock_guard lock(state->mutex);
    return state->shutdown;
}

FeedbackChannel make_feedback_channel(std::size_t capacity) {
    if (capacity == 0)
        throw std::invalid_argument("feedback capacity must be positive");

    auto state = std::make_shared<detail::FeedbackState>(capacity);
    return FeedbackChannel{
        FeedbackSender{state},
        FeedbackReceiver{std::move(state)}
    };
}

}
