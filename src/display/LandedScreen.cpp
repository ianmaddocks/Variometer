#include "display/LandedScreen.h"

#include <Arduino.h>

#include "display/DisplayManager.h"
#include "utils/Units.h"

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
    display.display().print(units::altitudeForDisplay(data.barometricAltitude, data.unitsImperial), 1);
    display.display().print(units::altitudeUnitLabel(data.unitsImperial));
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().print("Vario:");
    display.display().print(units::metersPerSecondForDisplay(data.verticalSpeed, data.unitsImperial), 2);
    display.display().print(units::metersPerSecondUnitLabel(data.unitsImperial));
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().print("Sats:");
    display.display().print(static_cast<int>(data.satellites));
}

void LandedScreen::exit() {}

}  // namespace variometer
