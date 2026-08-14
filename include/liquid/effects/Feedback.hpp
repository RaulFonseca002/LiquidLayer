#pragma once

#include "liquid/effects/EffectTypes.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace liquid {

namespace detail {
class FeedbackState;
}

enum class FeedbackSendResult {
    Sent,
    Full,
    Closed
};

class FeedbackReceiver;
struct FeedbackChannel;

class FeedbackSender {
    std::weak_ptr<detail::FeedbackState> state;

    explicit FeedbackSender(const std::shared_ptr<detail::FeedbackState>& sharedState);
    friend struct FeedbackChannel;
    friend FeedbackChannel make_feedback_channel(std::size_t capacity);

public:
    FeedbackSender() = default;

    FeedbackSendResult try_send(EffectReport report) const;
    FeedbackSendResult try_send(ExternalObservation observation) const;
    bool is_closed() const;
};

class FeedbackReceiver {
    std::shared_ptr<detail::FeedbackState> state;

    explicit FeedbackReceiver(std::shared_ptr<detail::FeedbackState> sharedState);
    friend struct FeedbackChannel;
    friend FeedbackChannel make_feedback_channel(std::size_t capacity);

public:
    FeedbackReceiver() = delete;
    FeedbackReceiver(const FeedbackReceiver&) = delete;
    FeedbackReceiver& operator=(const FeedbackReceiver&) = delete;
    FeedbackReceiver(FeedbackReceiver&&) noexcept = default;
    FeedbackReceiver& operator=(FeedbackReceiver&& other) noexcept;
    ~FeedbackReceiver();

    std::optional<EffectReport> try_receive();
    std::optional<ExternalObservation> try_receive_observation();
    std::vector<EffectReport> drain();
    std::vector<ExternalObservation> drain_observations();
    std::size_t pending() const;
    void shutdown();
    bool is_shutdown() const;
};

struct FeedbackChannel {
    FeedbackSender sender;
    FeedbackReceiver receiver;
};

FeedbackChannel make_feedback_channel(std::size_t capacity);

}
