#include "display/LandedScreen.h"

#include <Arduino.h>

#include "display/DisplayManager.h"

namespace variometer {

void LandedScreen::enter() {
    DBGLN("Entering landed screen");
}

void LandedScreen::update(const FlightData& data) {
    (void)data;
}

void LandedScreen::draw(DisplayManager& display, const FlightData& data) {

     int line = 3;
    display.display().setCursor(0, 1);
    display.display().print("Landed");
    display.display().setCursor(0, line++ *Config::LINE_SPACING);
    display.display().print("Time:");
    display.display().print(static_cast<int>(data.flightDuration / 60));
    display.display().print("m");
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().print("LZ:");
    display.display().print(data.distanceFromLZ, 1);
    display.display().print("km");
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().print("Alt:");
    display.display().print(data.barometricAltitude, 1);
    display.display().print("m");
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().print("Vario:");
    display.display().print(data.verticalSpeed, 2);
    display.display().print("m/s");
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().print("Sats:");
    display.display().print(static_cast<int>(data.satellites));
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().print("Press to reset");
}

void LandedScreen::exit() {}

}  // namespace variometer
