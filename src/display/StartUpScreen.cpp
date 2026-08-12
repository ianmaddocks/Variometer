#include "display/StartUpScreen.h"

#include <Arduino.h>

#include "display/DisplayManager.h"

namespace variometer {
namespace {
    String lastStartupStatus;
}

void StartUpScreen::enter() {
    lastStartupStatus = "";
    Serial.println("Entering startup screen");
}

void StartUpScreen::update(const FlightData& data) {
    (void)data;
}

void StartUpScreen::draw(DisplayManager& display, const FlightData& data) {
    String status = data.gpsFix ? "Startup screen: GPS ready" : "Startup screen: waiting for GPS lock";
    if (status != lastStartupStatus) {
        lastStartupStatus = status;
        Serial.println(status);
    }

    int line = 5;
    display.display().setCursor(0, line += Config::LINE_SPACING);
    display.display().print("Variometer");
    display.display().setCursor(0, line += Config::LINE_SPACING);
    display.display().print("v0.1.0");
    display.display().setCursor(0, line += Config::LINE_SPACING);
    display.display().print("Sats:");
    display.display().print(static_cast<int>(data.satellites));
    display.display().setCursor(0, line += Config::LINE_SPACING);
    display.display().print("Batt:");
    display.display().print(static_cast<int>(data.batteryPercent));
    display.display().print("%");
    display.display().setCursor(0, line += Config::LINE_SPACING);
    display.display().print(data.gpsFix ? "GPS ready" : "Waiting for GPS");
    display.display().setCursor(0, line += Config::LINE_SPACING);
    display.display().print("Need sats >=");
    display.display().print(Config::MIN_SATELLITES_DEFAULT);

    /*
    const int16_t barX = 8;
    const int16_t barY = 96;
    const int16_t barWidth = 112;
    const int16_t barHeight = 8;
    const int progress = (data.satellites * barWidth) / ((Config::MIN_SATELLITES_DEFAULT > 0) ? Config::MIN_SATELLITES_DEFAULT : 1);
    const int16_t filledWidth = static_cast<int16_t>(progress < barWidth ? progress : barWidth);

    display.display().drawRect(barX, barY, barWidth, barHeight, SH110X_WHITE);
    display.display().fillRect(barX, barY, filledWidth, barHeight, SH110X_WHITE);
    display.display().setCursor(8, 108);
    display.display().print("GPS lock progress");
    */
}

void StartUpScreen::exit() {}

}  // namespace variometer
