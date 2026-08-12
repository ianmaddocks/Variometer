#include "display/WindDirectionScreen.h"
#include <Arduino.h>
#include "display/DisplayManager.h"

namespace variometer {
namespace {
constexpr int16_t kVarioBoxW = 24;
constexpr int16_t kVarioBoxH = 96;
constexpr int16_t kRadius = 34;
constexpr int16_t kCenterX = 64-(kVarioBoxW/2)+10;
constexpr int16_t kCenterY = 128/2;
constexpr int16_t kVarioBoxX = 128-kVarioBoxW;
constexpr int16_t kVarioBoxY = 16;
String lastWindStatus;
}

void WindDirectionScreen::enter() {
    lastWindStatus = "";
    Serial.println("Entering wind direction screen");
}

void WindDirectionScreen::update(const FlightData& data) {
    (void)data;
}

void WindDirectionScreen::draw(DisplayManager& display, const FlightData& data) {
    String status = "Wind: " + String(data.windConfidence > 0.2f ? data.windSpeed : 0.0f, 1) + "m/s " +
                    String(static_cast<int>(data.windDirection)) + "deg";
    if (status != lastWindStatus) {
        lastWindStatus = status;
        Serial.println(status);
    }

    int line = 5;
    display.display().setCursor(0, line+=Config::LINE_SPACING);
    display.display().print("Wind");

    display.display().drawCircle(kCenterX, kCenterY, kRadius, SH110X_WHITE);

    if (data.windConfidence > 0.2f) {
        const float angleRad = (data.windDirection - 90.0f) * 0.0174532925f;
        const int16_t arrowX = kCenterX + static_cast<int16_t>(cosf(angleRad) * (kRadius - 10));
        const int16_t arrowY = kCenterY + static_cast<int16_t>(sinf(angleRad) * (kRadius - 10));
        display.display().drawLine(kCenterX, kCenterY, arrowX, arrowY, SH110X_WHITE);
        display.display().fillCircle(arrowX, arrowY, 3, SH110X_WHITE);
    }

    display.display().drawRect(kVarioBoxX, kVarioBoxY, kVarioBoxW, kVarioBoxH, SH110X_WHITE);
    const int16_t fillHeight = static_cast<int16_t>(constrain(abs(data.verticalSpeed) * 16.0f, 0.0f, 90.0f));
    if (data.verticalSpeed >= 0.0f) {
        display.display().fillRect(kVarioBoxX + 2, kVarioBoxY + kVarioBoxH - 2 - fillHeight,
                                   kVarioBoxW - 4, fillHeight, SH110X_WHITE);
    } else {
        display.display().fillRect(kVarioBoxX + 2, kVarioBoxY + kVarioBoxH - 2,
                                   kVarioBoxW - 4, fillHeight, SH110X_WHITE);
    }

    display.display().setCursor(0, 104);
    display.display().print("Wind:");
    display.display().print(data.windConfidence > 0.2f ? data.windSpeed : 0.0f, 1);
    display.display().print("m/s");
}

void WindDirectionScreen::exit() {}

}  // namespace variometer
