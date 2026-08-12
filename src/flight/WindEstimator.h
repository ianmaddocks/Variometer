#pragma once

#include "core/FlightData.h"

namespace variometer {

class WindEstimator {
public:
    WindEstimator() = default;
    void update(const FlightData& data);
    float getWindSpeed() const;
    float getWindDirection() const;
    float getWindConfidence() const;

private:
    float windSpeed_ = 0.0f;
    float windDirection_ = 0.0f;
    float windConfidence_ = 0.0f;
};

}  // namespace variometer
