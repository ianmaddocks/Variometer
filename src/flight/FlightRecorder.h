#pragma once

#include <stdint.h>

#include "core/FlightData.h"
#include "utils/RingBuffer.h"

namespace variometer {

struct TracePoint {
    float altitude = 0.0f;
    float timeSeconds = 0.0f;
    float latitude = 0.0f;
    float longitude = 0.0f;
};

class FlightRecorder {
public:
    FlightRecorder() = default;
    void clear();
    void addPoint(float altitude, float timeSeconds, float latitude = 0.0f, float longitude = 0.0f);
    size_t size() const;
    const TracePoint& at(size_t index) const;

private:
    RingBuffer<TracePoint, 128> history_;
};

}  // namespace variometer
