#pragma once

#include <stdint.h>

#include "config/Config.h"
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
    static constexpr size_t MAX_POINTS =
        (static_cast<size_t>(Config::FLIGHT_RECORDING_DURATION_MINUTES) * 60U * 1000U) /
        Config::ALTITUDE_TRACE_SAMPLE_INTERVAL_MS;
    RingBuffer<TracePoint, MAX_POINTS> history_;
};

}  // namespace variometer
