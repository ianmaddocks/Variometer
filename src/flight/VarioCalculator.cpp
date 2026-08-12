#include "flight/VarioCalculator.h"

#include <math.h>

namespace variometer {

void VarioCalculator::update(const FlightData& data) {
    const float raw = data.verticalSpeed;
    const float alpha = 0.25f;

    if (fabsf(raw) < 0.08f) {
        verticalSpeed_ = verticalSpeed_ * 0.85f;
    } else {
        const float filtered = verticalSpeed_ + alpha * (raw - verticalSpeed_);
        verticalSpeed_ = filtered;
    }

    if (fabsf(verticalSpeed_) < 0.05f) {
        verticalSpeed_ = 0.0f;
    }

    if (verticalSpeed_ > 5.0f) {
        verticalSpeed_ = 5.0f;
    } else if (verticalSpeed_ < -5.0f) {
        verticalSpeed_ = -5.0f;
    }

    lastRaw_ = raw;
}

float VarioCalculator::getVerticalSpeed() const { return verticalSpeed_; }

}  // namespace variometer
