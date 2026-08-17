#pragma once

#include <Arduino.h>

#include "core/FlightData.h"
#include "utils/RingBuffer.h"

namespace variometer {

class VarioCalculator {
public:
    VarioCalculator() = default;

    void reset();

    /*
     * Feed the calculator.
     *
     * sampleSequence / sampleTimeMs describe the altitude currently in
     * `data`. The calculator ingests a sample only when the sequence
     * changes, so it is safe -- and expected -- to call this every loop
     * pass regardless of how often the altitude source actually updates.
     *
     * This decoupling is deliberate. The calculator used to push one
     * sample per call and timestamp it with the call time, which meant
     * its behaviour depended entirely on loop timing: when the loop ran
     * slower than the sensor, history filled with duplicate altitudes
     * carrying distinct timestamps, and a regression over that staircase
     * produced wild, alternating vario readings.
     *
     * The source is passed in rather than read from a sensor directly so
     * the mock/GPS altitude feed can drive this identically.
     */
    void update(const FlightData& data,
                uint32_t sampleSequence,
                uint32_t sampleTimeMs);

    float getVerticalSpeed() const;
    float getRawVerticalSpeed() const;

private:
    struct Sample {
        float altitude;
        uint32_t timeMs;
    };

    /*
     * Must hold a full VARIO_REGRESSION_WINDOW_MS of samples at the
     * fastest rate the sensor can deliver. The MS5611 needs one pressure
     * plus one temperature conversion per reading (~40 ms at OSR 4096),
     * so ~25 Hz; a 1000 ms window therefore needs ~25 entries. 40 gives
     * headroom without meaningful memory cost (40 x 8 = 320 bytes).
     */
    static constexpr size_t HISTORY_SIZE = 40;

    RingBuffer<Sample, HISTORY_SIZE> history_;

    float verticalSpeed_ = 0.0f;
    float rawVerticalSpeed_ = 0.0f;

    // Sequence of the last sample actually ingested, used to reject
    // repeat calls that carry no new measurement.
    uint32_t lastSampleSequence_ = 0;
    bool hasSample_ = false;

    // Measurement time of the previous filter update, used to derive the
    // real elapsed time so the smoothing is independent of sample rate.
    uint32_t lastFilterTimeMs_ = 0;
    bool filterPrimed_ = false;

    // Counts accepted samples for Config::VARIO_TRACE_DECIMATION.
    uint8_t traceCounter_ = 0;
};

}  // namespace variometer