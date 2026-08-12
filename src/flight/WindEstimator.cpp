#include "flight/WindEstimator.h"

#include <math.h>

namespace variometer {

void WindEstimator::update(const FlightData& data) {
    if (data.gpsFix && data.groundSpeed > 2.0f && fabsf(data.verticalSpeed) < 2.0f) {
        const float apparentWind = data.groundSpeed * 0.35f;
        windSpeed_ = apparentWind;
        windDirection_ = data.track + 180.0f;
        windConfidence_ = 0.7f;
    } else if (data.gpsFix && data.groundSpeed > 1.0f) {
        windSpeed_ = data.groundSpeed * 0.2f;
        windDirection_ = data.track;
        windConfidence_ = 0.4f;
    } else if (data.flightState == FlightState::FLIGHT && fabsf(data.verticalSpeed) > 0.3f) {
        windSpeed_ = 0.0f;
        windDirection_ = 0.0f;
        windConfidence_ = 0.2f;
    } else {
        windSpeed_ = 0.0f;
        windDirection_ = 0.0f;
        windConfidence_ = 0.0f;
    }

    if (windConfidence_ > 0.0f && windSpeed_ > 0.0f) {
        windSpeed_ = fminf(windSpeed_, 15.0f);
        windDirection_ = fmodf(windDirection_, 360.0f);
        if (windDirection_ < 0.0f) {
            windDirection_ += 360.0f;
        }
    }
}

float WindEstimator::getWindSpeed() const { return windSpeed_; }
float WindEstimator::getWindDirection() const { return windDirection_; }
float WindEstimator::getWindConfidence() const { return windConfidence_; }

}  // namespace variometer
