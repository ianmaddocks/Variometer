#include "display/VarioScreen.h"

#include <Arduino.h>

#include "display/DisplayManager.h"

namespace variometer {
namespace {
    String lastVarioStatus;
}

void VarioScreen::enter() {
    lastVarioStatus = "";
    DBGLN("Entering variometer screen");
}

void VarioScreen::update(const FlightData& data) {
    (void)data;
}

void VarioScreen::draw(DisplayManager& display, const FlightData& data) {
    String status = "Vario: " + String(data.groundSpeed * 3.6f, 1) + "km/h " +
                    String(data.verticalSpeed, 2) + "m/s " + String(data.barometricAltitude, 1) + "m";
    if (status != lastVarioStatus) {
        lastVarioStatus = status;
        DBGLN(status);
    }

    int line = 3;
    display.display().setCursor(0, 1);
    display.display().print("flight ");
    display.display().print((int)(data.flightDuration / 60));
    display.display().print(" mins");
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().print("Alt:");
    display.display().print(data.barometricAltitude, 1);
    display.display().print("m");
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().print("Spd:");
    display.display().print(data.groundSpeed * 3.6f, 1);
    display.display().print("km/h");
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().print("Vario:");
    display.display().print(data.verticalSpeed, 2);
    display.display().print("m/s");
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().print("LZ:");
    display.display().print(data.distanceFromLZ, 1);
    display.display().print("km");
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().print("Batt:");
    display.display().print(static_cast<int>(data.batteryPercent));
    display.display().print("%");
    if (data.batteryPercent <= 20.0f) {
        display.display().print(" LOW");
    }
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().print("Sats:");
    display.display().print(static_cast<int>(data.satellites));
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().print("State:");
    display.display().print(static_cast<int>(data.flightState));
}

void VarioScreen::exit() {}

}  // namespace variometer
