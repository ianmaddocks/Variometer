#include "display/AltitudeTraceScreen.h"

#include <Arduino.h>

#include "display/DisplayManager.h"
#include "flight/FlightRecorder.h"

namespace variometer {
namespace {
constexpr int16_t kPlotX = 0;
constexpr int16_t kPlotY = 10;
constexpr int16_t kPlotW = 128;
constexpr int16_t kPlotH = 84;

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
    Serial.println("Entering altitude trace screen");
}

void AltitudeTraceScreen::update(const FlightData& data) {
    (void)data;
}

void AltitudeTraceScreen::draw(DisplayManager& display, const FlightData& data) {
    display.display().setCursor(0, 0);
    display.display().print("Alt trace");
    display.display().drawRect(kPlotX, kPlotY, kPlotW, kPlotH, SH110X_WHITE);

    if (data.tracePointCount < 2) {
        display.display().setCursor(8, (kPlotY + kPlotH / 2));
        display.display().print("No trace yet");
        return;
    }

    const int16_t plotLeft = kPlotX + 2;
    const int16_t plotRight = kPlotX + kPlotW - 2;
    const int16_t plotTop = kPlotY + 2;
    const int16_t plotBottom = kPlotY + kPlotH - 2;
    const int16_t plotHeight = plotBottom - plotTop;
    const int16_t plotWidth = plotRight - plotLeft;

    const FlightRecorder* recorder = display.recorder();
    const size_t totalCount = static_cast<size_t>(data.tracePointCount);
    const size_t maxVisible = static_cast<size_t>(plotWidth) + 1;
    const size_t visibleCount = (totalCount < maxVisible) ? totalCount : maxVisible;
    const size_t startIndex = totalCount - visibleCount;

    float minAlt = recorder->at(startIndex).altitude;
    float maxAlt = minAlt;
    for (size_t i = startIndex + 1; i < totalCount; ++i) {
        const float altitude = recorder->at(i).altitude;
        if (altitude < minAlt) minAlt = altitude;
        if (altitude > maxAlt) maxAlt = altitude;
    }
    const float span = maxAlt - minAlt;
    const float range = (span > 1.0f) ? span : 1.0f;

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

    display.display().setCursor(8, 100);
    display.display().print("Alt:");
    display.display().print(data.barometricAltitude, 1);
    display.display().print("m");
}

void AltitudeTraceScreen::exit() {}

}  // namespace variometer
