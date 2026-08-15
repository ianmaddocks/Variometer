#include "display/PreTakeoffScreen.h"

#include <Arduino.h>

#include "display/DisplayManager.h"
#include "config/Config.h"

namespace variometer {
namespace {
    String lastPreTakeoffStatus;
}

void PreTakeoffScreen::enter() {
    lastPreTakeoffStatus = "";
    DBGLN("Entering pre-takeoff screen");
}

void PreTakeoffScreen::update(const FlightData& data) {
    (void)data;
}

void PreTakeoffScreen::draw(DisplayManager& display, const FlightData& data) {
    String status = data.gpsFix ? "Pre-takeoff: GPS lock OK" : "Pre-takeoff: waiting for GPS";
    if (status != lastPreTakeoffStatus) {
        lastPreTakeoffStatus = status;
        DBGLN(status);
    }

    int line = 3;
    display.display().setCursor(0, 1);
    display.display().print("Ready to go");
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().print("Lat:");
    display.display().print(data.latitude, 4);
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().print("Lon:");
    display.display().print(data.longitude, 4);
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().print("Alt:");
    display.display().print(data.barometricAltitude, 1);
    display.display().print("m");
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().print("GPS lock OK");
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().print("Sats:");
    display.display().print(static_cast<int>(data.satellites));
}

void PreTakeoffScreen::exit() {}

}  // namespace variometer
