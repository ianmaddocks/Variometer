#include "display/AltitudeTraceScreen.h"

#include <Arduino.h>

#include "display/DisplayManager.h"
#include "flight/FlightRecorder.h"

namespace variometer {
namespace {
constexpr int16_t kPlotX = 0;
constexpr int16_t kPlotY = 22;
constexpr int16_t kPlotW = 128;
#ifdef DEBUG
    constexpr int16_t kPlotH = 77;
#else
    constexpr int16_t kPlotH = 90;
#endif

int16_t clampY(int16_t value, int16_t minValue, int16_t maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}
}  // namespace

void AltitudeTraceScreen::enter() {
    DBGLN("Entering altitude trace screen");
}

void AltitudeTraceScreen::update(const FlightData& data) {
    (void)data;
}

void AltitudeTraceScreen::draw(DisplayManager& display, const FlightData& data) {
    /*
     * In flight this screen scrolls: the visible window is always the
     * newest samples, anchored to the right edge, because the pilot
     * wants "what just happened." Once the flight is over there is no
     * "just happened" left -- landing without switching modes would
     * either freeze on the last few minutes (LANDING_DETECTED/
     * POST_FLIGHT can hold for a while) or require the pilot to somehow
     * scroll back through a flight that may have lasted hours. Showing
     * the whole flight, oldest-to-newest left-to-right, is what a
     * post-flight review actually needs.
     */
    const bool showFullFlight = (data.flightState == FlightState::LANDING_DETECTED ||
                                 data.flightState == FlightState::POST_FLIGHT);

    // Title is intentionally kept simple so the plot remains the dominant element.
    display.display().setCursor(0, 1);
    display.display().print(showFullFlight ? "Alt trace (full)" : "Alt trace");
    //display.display().drawRect(kPlotX, kPlotY, kPlotW, kPlotH, SH110X_WHITE);

    // If there are not enough samples yet, keep the screen readable and explain the empty state.
    if (data.tracePointCount < 2) {
        display.display().setCursor(8, (kPlotY + kPlotH / 2));
        display.display().print("No trace yet");
        return;
    }

    // Plot bounds are kept inset from the screen edge so the trace has a small margin.
    const int16_t plotLeft = kPlotX + 0;
    const int16_t plotRight = kPlotX + kPlotW - 0;
    const int16_t plotTop = kPlotY + 2;
    const int16_t plotBottom = kPlotY + kPlotH - 2;
    const int16_t plotHeight = plotBottom - plotTop;
    const int16_t plotWidth = plotRight - plotLeft;

    const FlightRecorder* recorder = display.recorder();
    const size_t totalCount = static_cast<size_t>(data.tracePointCount);

    /*
     * Two different mappings from recorded samples to plot columns:
     *
     *   live   -- only the newest plotWidth+1 samples, right-anchored,
     *             one column per sample, so the trace scrolls smoothly
     *             as new points arrive.
     *   full   -- always exactly plotWidth+1 columns spanning the whole
     *             recorded trace, oldest at the left edge, newest at the
     *             right. sampleIndex() nearest-neighbour maps each
     *             column back into [0, totalCount). This both downsamples
     *             a long flight (which can hold thousands of points, up
     *             to Config::FLIGHT_RECORDING_DURATION_MINUTES) and
     *             stretches a short one to fill the width -- a 30-second
     *             test flight should show its shape across the whole
     *             plot, not sit as a few-pixel sliver at the left edge.
     */
    size_t startIndex = 0;
    size_t visibleCount = 0;

    if (showFullFlight) {
        visibleCount = static_cast<size_t>(plotWidth) + 1;
    } else {
        const size_t maxVisible = static_cast<size_t>(plotWidth) + 1;
        visibleCount = (totalCount < maxVisible) ? totalCount : maxVisible;
        startIndex = totalCount - visibleCount;
    }

    const auto sampleIndex = [&](size_t column) -> size_t {
        if (!showFullFlight) {
            return startIndex + column;
        }
        return (column * (totalCount - 1)) / (visibleCount - 1);
    };

    /*
     * Scale to the true altitude range of what's being shown.
     *
     * In full-flight mode this deliberately uses FlightData's
     * traceAltitudeMin/Max -- computed by Application over every
     * recorded point -- rather than rescanning only the downsampled
     * columns here. Resampling can step past the single sample that hit
     * the flight's actual peak or trough; using the precomputed true
     * range keeps the printed min/max honest even when the drawn line
     * has smoothed over a brief spike between columns.
     */
    float minAlt;
    float maxAlt;

    if (showFullFlight) {
        minAlt = data.traceAltitudeMin;
        maxAlt = data.traceAltitudeMax;
    } else {
        minAlt = recorder->at(startIndex).altitude;
        maxAlt = minAlt;
        for (size_t i = startIndex + 1; i < totalCount; ++i) {
            const float altitude = recorder->at(i).altitude;
            if (altitude < minAlt) minAlt = altitude;
            if (altitude > maxAlt) maxAlt = altitude;
        }
    }
    const float span = maxAlt - minAlt;
    const float range = (span > 1.0f) ? span : 1.0f;

    // Draw reference bands at the minimum and maximum altitude in the current window.
     int16_t minY = plotBottom - static_cast<int16_t>(((minAlt - minAlt) / range) * plotHeight);
     int16_t maxY = plotBottom - static_cast<int16_t>(((maxAlt - minAlt) / range) * plotHeight);
    minY = kPlotY;
    maxY = kPlotY + kPlotH;

    for (int16_t x = plotLeft; x <= plotRight; x += 3) {
        display.display().drawPixel(x, maxY, SH110X_WHITE);
    }
    for (int16_t x = plotLeft; x <= plotRight; x += 3) {
        display.display().drawPixel(x, minY, SH110X_WHITE);
    }

    /*
     * Plot each consecutive column.
     *
     * Live mode draws right-to-left in the sense that the newest sample
     * is pinned to plotRight (unchanged from before). Full-flight mode
     * is simply left-anchored: column 0 at plotLeft, one pixel per
     * column, since sampleIndex() has already done the resampling.
     */
    for (size_t i = 0; i + 1 < visibleCount; ++i) {
        const TracePoint& a = recorder->at(sampleIndex(i));
        const TracePoint& b = recorder->at(sampleIndex(i + 1));

        int16_t x0, x1;
        if (showFullFlight) {
            x0 = plotLeft + static_cast<int16_t>(i);
            x1 = plotLeft + static_cast<int16_t>(i + 1);
        } else {
            x0 = plotRight - static_cast<int16_t>(visibleCount - 1 - i);
            x1 = plotRight - static_cast<int16_t>(visibleCount - 1 - (i + 1));
        }

        const int16_t y0 = plotBottom - static_cast<int16_t>(((a.altitude - minAlt) / range) * plotHeight);
        const int16_t y1 = plotBottom - static_cast<int16_t>(((b.altitude - minAlt) / range) * plotHeight);
        display.display().drawLine(x0, clampY(y0, plotTop, plotBottom),
                                   x1, clampY(y1, plotTop, plotBottom), SH110X_WHITE);
    }

    // Label each reference line with its altitude value so the scale is readable.
    char minLabel[16];
    char maxLabel[16];
    snprintf(minLabel, sizeof(minLabel), "%.0fm", minAlt);
    snprintf(maxLabel, sizeof(maxLabel), "%.0fm", maxAlt);
    display.display().fillRect(47, minY - 6, strlen(maxLabel)*7.5, 13, SH110X_WHITE);
    display.display().fillRect(47, maxY - 6, strlen(minLabel)*7.5, 13, SH110X_WHITE);
    display.display().setTextColor(SH110X_BLACK);
    display.display().setCursor(50, maxY - 3);
    display.display().print(minLabel);
    display.display().setCursor(50, minY - 3);
    display.display().print(maxLabel);
    display.display().setTextColor(SH110X_WHITE);

    // Show the live altitude value beneath the plot to make the current reading immediately visible.
    //display.display().setCursor(8, 100);
    //display.display().print("Alt:");
    //display.display().print(data.barometricAltitude, 1);
    //display.display().print("m");
}

void AltitudeTraceScreen::exit() {}

}  // namespace variometer
