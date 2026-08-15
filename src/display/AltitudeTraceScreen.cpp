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
    // Title is intentionally kept simple so the plot remains the dominant element.
    display.display().setCursor(0, 1);
    display.display().print("Alt trace");
    //display.display().drawRect(kPlotX, kPlotY, kPlotW, kPlotH, SH110X_WHITE);

    // If there are not enough samples yet, keep the screen readable and explain the empty state.
    if (data.tracePointCount < 2) {
        display.display().setCursor(8, (kPlotY + kPlotH / 2));
        display.display().print("No trace yet");
        return;
    }

    // Plot bounds are kept inset from the screen edge so the trace has a small margin.
    const int16_t plotLeft = kPlotX + 2;
    const int16_t plotRight = kPlotX + kPlotW - 2;
    const int16_t plotTop = kPlotY + 2;
    const int16_t plotBottom = kPlotY + kPlotH - 2;
    const int16_t plotHeight = plotBottom - plotTop;
    const int16_t plotWidth = plotRight - plotLeft;

    // Use the recorder as the source of truth and display only the newest samples.
    // This keeps the trace scrolling left-to-right without consuming extra memory.
    const FlightRecorder* recorder = display.recorder();
    const size_t totalCount = static_cast<size_t>(data.tracePointCount);
    const size_t maxVisible = static_cast<size_t>(plotWidth) + 1;
    const size_t visibleCount = (totalCount < maxVisible) ? totalCount : maxVisible;
    const size_t startIndex = totalCount - visibleCount;

    // Scale the entire visible trace to the minimum and maximum altitude in the current window.
    // A minimum span avoids division by zero while preserving a usable display range.
    float minAlt = recorder->at(startIndex).altitude;
    float maxAlt = minAlt;
    for (size_t i = startIndex + 1; i < totalCount; ++i) {
        const float altitude = recorder->at(i).altitude;
        if (altitude < minAlt) minAlt = altitude;
        if (altitude > maxAlt) maxAlt = altitude;
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

    // Plot each consecutive point in screen space, drawing from right to left so the
    // newest sample remains at the right edge as the trace scrolls forward in time.
    for (size_t i = 0; i + 1 < visibleCount; ++i) {
        const TracePoint& a = recorder->at(startIndex + i);
        const TracePoint& b = recorder->at(startIndex + i + 1);
        const int16_t x0 = plotRight - static_cast<int16_t>(visibleCount - 1 - i);
        const int16_t x1 = plotRight - static_cast<int16_t>(visibleCount - 1 - (i + 1));
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
    display.display().fillRect(47, minY - 6, strlen(maxLabel)*7, 13, SH110X_WHITE);
    display.display().fillRect(47, maxY - 6, strlen(minLabel)*7, 13, SH110X_WHITE);
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
