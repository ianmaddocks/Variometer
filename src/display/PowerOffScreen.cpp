#include "display/PowerOffScreen.h"
#include <Arduino.h>
#include "display/DisplayManager.h"

namespace variometer {
namespace {
#ifdef DEBUG
    String lastPowerOffStatus;
#endif
}

void PowerOffScreen::enter() {
#ifdef DEBUG
    lastPowerOffStatus = "";
#endif
    DBGLN("Entering power-off screen");
}

void PowerOffScreen::update(const FlightData& data) {
    (void)data;
}

void PowerOffScreen::draw(DisplayManager& display, const FlightData& data) {
    // See VarioScreen.cpp for why this is compiled out entirely in
    // release builds rather than left as a DBGLN no-op.
#ifdef DEBUG
    String status = "Power Off: batt=" + String(static_cast<int>(data.batteryPercent)) + "% sats=" + String(static_cast<int>(data.satellites));
    if (status != lastPowerOffStatus) {
        lastPowerOffStatus = status;
        DBGLN(status);
    }
#endif

    int line = 3;
    display.display().setCursor(0, 1);
    display.display().print("Power Off");
    display.display().setCursor(0, line++ *Config::LINE_SPACING);
    display.display().print("2x press to power off");
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().print("Batt:");
    display.display().print(static_cast<int>(data.batteryPercent));
    display.display().print("%  Sats:");
    display.display().print(static_cast<int>(data.satellites));
}

void PowerOffScreen::exit() {}

}  // namespace variometer
