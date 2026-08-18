#pragma once

#include <stdint.h>

#include "core/FlightData.h"
#include "utils/RingBuffer.h"

namespace variometer {

/*
 * Wind speed/direction derived from the drift in ground speed while
 * circling -- the standard "poor man's" wind estimate used by most
 * gliding/paragliding vario computers.
 *
 * At constant airspeed, ground speed varies with heading relative to the
 * wind: lowest flying into it (heading = the wind's FROM direction),
 * highest flying with it. Over one or more turns:
 *
 *   windSpeed     = (maxGroundSpeed - minGroundSpeed) / 2
 *   windDirection = heading at minimum ground speed
 *
 * This only holds while genuinely turning through a range of headings.
 * On straight flight, ground-speed variation has other causes (throttle,
 * bar pressure, terrain) and this would misattribute it as wind. Rather
 * than add explicit turn detection, confidence is derived from how much
 * of the compass the sampled headings actually cover: straight flight
 * naturally covers a narrow arc, so confidence stays low on its own --
 * which is the behaviour a turn detector would have produced anyway, at
 * a fraction of the complexity.
 */
class WindEstimator {
public:
    WindEstimator() = default;

    /*
     * sampleSequence lets the caller offer this every loop pass while
     * only genuinely new GPS fixes are ingested. track/groundSpeed do
     * not change between fixes; feeding duplicates would not add
     * heading coverage, just waste ring-buffer capacity on repeats of
     * the same point (see VarioCalculator for the same problem solved
     * the same way on the altitude side).
     */
    void update(const FlightData& data, uint32_t sampleSequence);

    float getWindSpeed() const;
    float getWindDirection() const;
    float getWindConfidence() const;

private:
    struct Sample {
        float headingDeg;
        float speedMs;
        uint32_t timeMs;
    };

    void reset();
    void recompute(uint32_t nowMs);

    // At ~1 fix/sec this holds ~48s of history, comfortably more than
    // Config::WIND_ESTIMATE_WINDOW_MS; entries older than the window are
    // skipped by recompute() regardless of whether they are still
    // physically present in the ring.
    static constexpr size_t HISTORY_SIZE = 48;
    RingBuffer<Sample, HISTORY_SIZE> history_;

    uint32_t lastSampleSequence_ = 0;
    bool hasSample_ = false;

    // Tracked so a fresh flight starts with a clean history rather than
    // carrying over drift estimated during a previous flight.
    FlightState lastFlightState_ = FlightState::PREFLIGHT;

    float windSpeed_ = 0.0f;
    float windDirection_ = 0.0f;
    float windConfidence_ = 0.0f;
};

}  // namespace variometer
