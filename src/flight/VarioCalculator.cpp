#include "flight/VarioCalculator.h"

#include <math.h>

#include "config/Config.h"

namespace variometer {

void VarioCalculator::reset() {
    history_.clear();

    verticalSpeed_ = 0.0f;
    rawVerticalSpeed_ = 0.0f;

    initialised_ = false;
}

void VarioCalculator::update(const FlightData& data) {
    const uint32_t now = millis();

    const float altitude = data.barometricAltitude;

    if (!isfinite(altitude)) {
        return;
    }

    // Add the latest altitude sample.
    history_.push({
        altitude,
        now
    });

    if (history_.size() < 3) {
        return;
    }

    /*
     * We want approximately the configured regression window.
     *
     * At the moment Application::updateFlightLogic() runs every
     * 50 ms, so a 300 ms window gives us roughly six samples.
     */

    const Sample& newest =
        history_.at(history_.size() - 1);

    /*
     * Linear regression:
     *
     *      y = altitude
     *      x = time
     *
     * The gradient is the vertical speed.
     *
     * Using all samples rather than simply:
     *
     *      newestAltitude - oldestAltitude
     *
     * makes the result considerably less sensitive to individual
     * pressure/altitude noise.
     */

    double sumX = 0.0;
    double sumY = 0.0;
    double sumXY = 0.0;
    double sumXX = 0.0;

    uint32_t sampleCount = 0;

    for (size_t i = 0; i < history_.size(); ++i) {

        const Sample& sample = history_.at(i);

        const uint32_t ageMs =
            newest.timeMs - sample.timeMs;

        if (ageMs > Config::VARIO_REGRESSION_WINDOW_MS) {
            continue;
        }

        // Work backwards from newest sample.
        const double x =
            -static_cast<double>(ageMs) / 1000.0;

        const double y =
            static_cast<double>(sample.altitude);

        sumX += x;
        sumY += y;
        sumXY += x * y;
        sumXX += x * x;

        ++sampleCount;
    }

    if (sampleCount < 3) {
        return;
    }

    const double denominator =
        (static_cast<double>(sampleCount) * sumXX) -
        (sumX * sumX);

    if (fabs(denominator) < 1e-9) {
        return;
    }

    const double slope =
        (
            (static_cast<double>(sampleCount) * sumXY) -
            (sumX * sumY)
        ) /
        denominator;

    const float raw =
        static_cast<float>(slope);

    if (!isfinite(raw)) {
        return;
    }

    /*
     * Reject impossible measurements.
     *
     * This is deliberately wider than the display range.
     * The display/audio can be limited to +/-5 m/s, but we don't
     * want to throw away a genuine stronger climb or sink.
     */
    if (raw > Config::VARIO_CALC_MAX ||
        raw < -Config::VARIO_CALC_MAX) {
        return;
    }

    rawVerticalSpeed_ = raw;

    /*
     * Exponential low-pass filter.
     *
     * A higher alpha responds faster.
     * A lower alpha is smoother.
     */
    const float alpha =
        Config::VARIO_FILTER_ALPHA;

    verticalSpeed_ +=
        alpha *
        (rawVerticalSpeed_ - verticalSpeed_);

    /*
     * Small deadband around zero.
     *
     * This prevents the instrument from continually reporting
     * tiny climb/sink values while stationary.
     */
    if (fabsf(verticalSpeed_) <
        Config::VARIO_DEADBAND) {

        verticalSpeed_ *=
            Config::VARIO_ZERO_DECAY;

        if (fabsf(verticalSpeed_) <
            Config::VARIO_ZERO_THRESHOLD) {

            verticalSpeed_ = 0.0f;
        }
    }

    /*
     * Do NOT use the display limits here.
     *
     * Keep the actual calculated value available to the audio
     * system. The display can clamp independently.
     */
}

float VarioCalculator::getVerticalSpeed() const {
    return verticalSpeed_;
}

float VarioCalculator::getRawVerticalSpeed() const {
    return rawVerticalSpeed_;
}

}  // namespace variometer