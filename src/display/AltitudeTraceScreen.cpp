#include "display/AltitudeTraceScreen.h"

#include <Arduino.h>

#include "display/DisplayManager.h"
#include "flight/FlightRecorder.h"

namespace variometer {
namespace {
constexpr int16_t kPlotX = 8;
constexpr int16_t kPlotY = 18;
constexpr int16_t kPlotW = 112;
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
        display.display().setCursor(8, 48);
        display.display().print("No trace yet");
        return;
    }

    const float minAlt = data.traceAltitudeMin;
    const float maxAlt = data.traceAltitudeMax;
    const float span = maxAlt - minAlt;
    const float range = (span > 1.0f) ? span : 1.0f;

    for (int16_t i = 0; i < static_cast<int16_t>(data.tracePointCount) - 1; ++i) {
        const TracePoint& a = display.recorder()->at(i);
        const TracePoint& b = display.recorder()->at(i + 1);
        const int16_t x0 = kPlotX + 2 + i * 2;
        const int16_t x1 = kPlotX + 2 + (i + 1) * 2;
        const int16_t y0 = kPlotY + kPlotH - 2 - static_cast<int16_t>(((a.altitude - minAlt) / range) * (kPlotH - 4));
        const int16_t y1 = kPlotY + kPlotH - 2 - static_cast<int16_t>(((b.altitude - minAlt) / range) * (kPlotH - 4));
        display.display().drawLine(x0, clampY(y0, kPlotY + 2, kPlotY + kPlotH - 2),
                                   x1, clampY(y1, kPlotY + 2, kPlotY + kPlotH - 2), SH110X_WHITE);
    }

    display.display().setCursor(8, 108);
    display.display().print("Alt:");
    display.display().print(data.barometricAltitude, 1);
    display.display().print("m");
}

void AltitudeTraceScreen::exit() {}

}  // namespace variometer
