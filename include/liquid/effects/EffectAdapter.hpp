#pragma once

#include "liquid/effects/EffectTypes.hpp"
#include "liquid/effects/Feedback.hpp"

namespace liquid {

class EffectAdapter {
public:
    virtual ~EffectAdapter() = default;

    virtual const AdapterRoute& route() const = 0;
    virtual AdapterCapabilities capabilities() const = 0;
    virtual DispatchResult dispatch(
        const EffectCommand& command,
        FeedbackSender feedback
    ) = 0;
};

}
