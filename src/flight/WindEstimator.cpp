#include "flight/WindEstimator.h"

#include <Arduino.h>
#include <math.h>

#include "config/Config.h"

namespace variometer {
namespace {

// Heading is bucketed into 45deg sectors; confidence is the fraction of
// the 8 sectors actually visited within the current window. Coarser than
// this and confidence would stay artificially high after sampling only
// a rough quarter of the circle; finer and normal GPS heading noise
// would make coverage flicker between adjacent sectors.
constexpr int kSectorCount = 8;
constexpr float kSectorWidthDeg = 360.0f / static_cast<float>(kSectorCount);

bool isFlying(FlightState state) {
    return state == FlightState::FLIGHT || state == FlightState::TAKEOFF_DETECTED;
}

}  // namespace

void WindEstimator::reset() {
    history_.clear();
    hasSample_ = false;
    lastSampleSequence_ = 0;

    windSpeed_ = 0.0f;
    windDirection_ = 0.0f;
    windConfidence_ = 0.0f;
}

void WindEstimator::update(const FlightData& data, uint32_t sampleSequence) {
    const bool flying = isFlying(data.flightState);

    if (!flying) {
        // Leaving flight: drop history so it cannot leak into the next
        // flight's estimate. Checked on the transition, not every call,
        // so reset() is not fighting the confidence computation below
        // every single preflight update.
        if (isFlying(lastFlightState_)) {
            reset();
        }
        lastFlightState_ = data.flightState;
        windConfidence_ = 0.0f;
        return;
    }
    lastFlightState_ = data.flightState;

    // Only ingest a genuinely new fix. track/groundSpeed hold their last
    // value between fixes, and this function is polled far more often
    // than fixes arrive.
    if (hasSample_ && sampleSequence == lastSampleSequence_) {
        return;
    }
    lastSampleSequence_ = sampleSequence;
    hasSample_ = true;

    if (!data.gpsFix || !isfinite(data.groundSpeed) || !isfinite(data.track)) {
        return;
    }

    // Too slow for heading to mean anything -- ground handling, the
    // moment after touchdown, or station-keeping in very light lift.
    if (data.groundSpeed < Config::WIND_ESTIMATE_MIN_SPEED_MS) {
        return;
    }

    const uint32_t now = millis();
    history_.push({data.track, data.groundSpeed, now});
    recompute(now);
}

void WindEstimator::recompute(uint32_t nowMs) {
    float minSpeed = 1.0e9f;
    float maxSpeed = -1.0e9f;
    float headingAtMinSpeed = 0.0f;
    bool sectorSeen[kSectorCount] = {false};
    size_t consideredCount = 0;

    for (size_t i = 0; i < history_.size(); ++i) {
        const Sample& sample = history_.at(i);
        const uint32_t age = nowMs - sample.timeMs;

        if (age > Config::WIND_ESTIMATE_WINDOW_MS) {
            continue;  // aged out of the window
        }

        ++consideredCount;

        if (sample.speedMs < minSpeed) {
            minSpeed = sample.speedMs;
            headingAtMinSpeed = sample.headingDeg;
        }
        if (sample.speedMs > maxSpeed) {
            maxSpeed = sample.speedMs;
        }

        int sector = static_cast<int>(sample.headingDeg / kSectorWidthDeg);
        if (sector < 0) {
            sector = 0;
        } else if (sector >= kSectorCount) {
            sector = kSectorCount - 1;
        }
        sectorSeen[sector] = true;
    }

    // Four points is the minimum for "lowest" and "highest" to mean
    // anything at all; sector coverage below is what actually gates
    // whether the result is trusted for display.
    if (consideredCount < 4) {
        windConfidence_ = 0.0f;
        return;
    }

    int sectorsCovered = 0;
    for (int i = 0; i < kSectorCount; ++i) {
        if (sectorSeen[i]) {
            ++sectorsCovered;
        }
    }

    windSpeed_ = fminf(fmaxf((maxSpeed - minSpeed) * 0.5f, 0.0f),
                       Config::WIND_ESTIMATE_MAX_MS);
    windDirection_ = fmodf(headingAtMinSpeed + 360.0f, 360.0f);

    /*
     * Confidence is purely the fraction of the compass actually sampled
     * within the window. This is what makes the estimator safe without
     * explicit turn detection: straight-line flight covers a narrow arc
     * of headings, so confidence stays low on its own, and a stale
     * estimate from a thermal the aircraft has since left decays as
     * those samples age out of the window (see the class comment).
     */
    windConfidence_ =
        static_cast<float>(sectorsCovered) / static_cast<float>(kSectorCount);
}

float WindEstimator::getWindSpeed() const { return windSpeed_; }
float WindEstimator::getWindDirection() const { return windDirection_; }
float WindEstimator::getWindConfidence() const { return windConfidence_; }

}  // namespace variometer
