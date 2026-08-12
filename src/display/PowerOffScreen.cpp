#include "display/PowerOffScreen.h"
#include <Arduino.h>
#include "display/DisplayManager.h"

namespace variometer {
namespace {
    String lastPowerOffStatus;
}

void PowerOffScreen::enter() {
    lastPowerOffStatus = "";
    Serial.println("Entering power-off screen");
}

void PowerOffScreen::update(const FlightData& data) {
    (void)data;
}

void PowerOffScreen::draw(DisplayManager& display, const FlightData& data) {
    String status = "Power Off: batt=" + String(static_cast<int>(data.batteryPercent)) + "% sats=" + String(static_cast<int>(data.satellites));
    if (status != lastPowerOffStatus) {
        lastPowerOffStatus = status;
        Serial.println(status);
    }

    int line = 5;
    display.display().setCursor(0, line+=Config::LINE_SPACING);
    display.display().print("Power Off");
    display.display().setCursor(0, line+=Config::LINE_SPACING);
    display.display().print("Hold to power down");
    display.display().setCursor(0, line+=Config::LINE_SPACING);
    display.display().print("Press again to cancel");
    display.display().setCursor(0, line+=Config::LINE_SPACING);
    display.display().print("Batt:");
    display.display().print(static_cast<int>(data.batteryPercent));
    display.display().print("%  Sats:");
    display.display().print(static_cast<int>(data.satellites));
}

void PowerOffScreen::exit() {}

}  // namespace variometer
