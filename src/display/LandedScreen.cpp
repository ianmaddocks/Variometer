#include "display/LandedScreen.h"

#include <Arduino.h>

#include "display/DisplayManager.h"

namespace variometer {

void LandedScreen::enter() {
    Serial.println("Entering landed screen");
}

void LandedScreen::update(const FlightData& data) {
    (void)data;
}

void LandedScreen::draw(DisplayManager& display, const FlightData& data) {

    display.display().setCursor(0, 0);
    display.display().print("Landed");
    display.display().setCursor(0, 18);
    display.display().print("Time:");
    display.display().print(static_cast<int>(data.flightDuration / 60));
    display.display().print("m");
    display.display().setCursor(0, 36);
    display.display().print("LZ:");
    display.display().print(data.distanceFromLZ, 1);
    display.display().print("km");
    display.display().setCursor(0, 54);
    display.display().print("Alt:");
    display.display().print(data.barometricAltitude, 1);
    display.display().print("m");
    display.display().setCursor(0, 72);
    display.display().print("Vario:");
    display.display().print(data.verticalSpeed, 2);
    display.display().print("m/s");
    display.display().setCursor(0, 90);
    display.display().print("Sats:");
    display.display().print(static_cast<int>(data.satellites));
    display.display().setCursor(0, 108);
    display.display().print("Press to reset");
}

void LandedScreen::exit() {}

}  // namespace variometer
