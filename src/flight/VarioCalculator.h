#pragma once

#include <Arduino.h>

#include "core/FlightData.h"
#include "utils/RingBuffer.h"

namespace variometer {

class VarioCalculator {
public:
    VarioCalculator() = default;

    void reset();
    void update(const FlightData& data);

    float getVerticalSpeed() const;
    float getRawVerticalSpeed() const;

private:
    struct Sample {
        float altitude;
        uint32_t timeMs;
    };

    static constexpr size_t HISTORY_SIZE = 16;

    RingBuffer<Sample, HISTORY_SIZE> history_;

    float verticalSpeed_ = 0.0f;
    float rawVerticalSpeed_ = 0.0f;

    bool initialised_ = false;
};

}  // namespace variometer