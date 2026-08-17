#include "flight/VarioCalculator.h"

#include <math.h>

#include "config/Config.h"

namespace variometer {

void VarioCalculator::reset() {
    history_.clear();

    verticalSpeed_ = 0.0f;
    rawVerticalSpeed_ = 0.0f;

    hasSample_ = false;
    lastSampleSequence_ = 0;

    filterPrimed_ = false;
    lastFilterTimeMs_ = 0;
}

void VarioCalculator::update(const FlightData& data,
                             uint32_t sampleSequence,
                             uint32_t sampleTimeMs) {
    const float altitude = data.barometricAltitude;

    if (!isfinite(altitude)) {
        return;
    }

    /*
     * Ingest only genuinely new measurements.
     *
     * Callers poll far more often than the sensor produces readings. If
     * we pushed on every call, history would contain runs of identical
     * altitudes at increasing timestamps -- a staircase -- and the
     * regression below would report a slope determined by where the step
     * happened to fall inside the window rather than by the real climb
     * rate. That is what made the displayed vario alternate between
     * frozen and wildly wrong.
     */
    if (hasSample_ && sampleSequence == lastSampleSequence_) {
        return;
    }

    lastSampleSequence_ = sampleSequence;
    hasSample_ = true;

    // Timestamp with the measurement time supplied by the source, not
    // millis() here, which would include the poll delay.
    history_.push({
        altitude,
        sampleTimeMs
    });

    if (history_.size() < 3) {
        return;
    }

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

    // Age of the oldest sample inside the window, i.e. the regression
    // baseline. Checked below before the slope is trusted.
    uint32_t spanMs = 0;

    for (size_t i = 0; i < history_.size(); ++i) {

        const Sample& sample = history_.at(i);

        const uint32_t ageMs =
            newest.timeMs - sample.timeMs;

        if (ageMs > Config::VARIO_REGRESSION_WINDOW_MS) {
            continue;
        }

        if (ageMs > spanMs) {
            spanMs = ageMs;
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

    /*
     * Require a meaningful baseline before trusting the gradient.
     *
     * Immediately after reset() the window holds a few closely spaced
     * samples; dividing a small altitude change by a very short baseline
     * amplifies sensor noise into a large false climb rate. Waiting for
     * a real span costs a fraction of a second at startup and removes
     * the spurious spike that followed every takeoff reset.
     */
    if (spanMs < Config::VARIO_MIN_REGRESSION_SPAN_MS) {
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
     * Elapsed time since the previous filter update, taken from the
     * measurement timestamps rather than assumed from a fixed interval.
     */
    uint32_t dtMs = 0;

    if (filterPrimed_) {
        dtMs = newest.timeMs - lastFilterTimeMs_;
    }

    lastFilterTimeMs_ = newest.timeMs;

    if (!filterPrimed_) {
        // First trusted slope: adopt it directly instead of easing up
        // from zero, which would otherwise read as a slow false sink.
        filterPrimed_ = true;
        verticalSpeed_ = rawVerticalSpeed_;
        return;
    }

    if (dtMs == 0) {
        return;
    }

    /*
     * Exponential low-pass filter, expressed as a time constant.
     *
     *      alpha = dt / (tau + dt)
     *
     * Deriving alpha from the real elapsed time means the filter has the
     * same physical response no matter how fast samples arrive. The
     * previous fixed alpha of 0.25 quietly became a different filter
     * every time loop timing changed.
     *
     * Because the longer regression window already removes most of the
     * noise, tau can stay short here without the reading becoming jumpy.
     */
    const float dtSeconds =
        static_cast<float>(dtMs) / 1000.0f;

    const float tauSeconds =
        static_cast<float>(Config::VARIO_FILTER_TAU_MS) / 1000.0f;

    const float alpha =
        dtSeconds / (tauSeconds + dtSeconds);

    verticalSpeed_ +=
        alpha *
        (rawVerticalSpeed_ - verticalSpeed_);

    /*
     * Small deadband around zero.
     *
     * This prevents the instrument from continually reporting
     * tiny climb/sink values while stationary.
     *
     * The decay is also time-based. It was previously a fixed multiply
     * applied once per update, so how quickly the reading settled to
     * zero depended on how often the loop happened to run.
     */
    if (fabsf(verticalSpeed_) <
        Config::VARIO_DEADBAND) {

        const float decayTauSeconds =
            static_cast<float>(Config::VARIO_ZERO_DECAY_TAU_MS) / 1000.0f;

        // First-order approximation of exp(-dt / tau); cheaper than
        // expf() on a soft-float target and accurate for dt << tau.
        float decay =
            1.0f - (dtSeconds / decayTauSeconds);

        if (decay < 0.0f) {
            decay = 0.0f;
        }

        verticalSpeed_ *= decay;

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