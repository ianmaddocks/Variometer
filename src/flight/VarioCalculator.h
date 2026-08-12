#pragma once

#include "core/FlightData.h"

namespace variometer {

class VarioCalculator {
public:
    VarioCalculator() = default;
    void update(const FlightData& data);
    float getVerticalSpeed() const;

private:
    float verticalSpeed_ = 0.0f;
    float lastRaw_ = 0.0f;
};

}  // namespace variometer
