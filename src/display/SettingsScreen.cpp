#include "display/SettingsScreen.h"

#include <Arduino.h>

#include "display/DisplayManager.h"

namespace variometer {
namespace {
#ifdef DEBUG
    String lastSettingsStatus;
#endif
}

void SettingsScreen::enter() {
#ifdef DEBUG
    lastSettingsStatus = "";
#endif
    DBGLN("Entering settings screen");
}

void SettingsScreen::update(const FlightData& data) {
    (void)data;
}

void SettingsScreen::draw(DisplayManager& display, const FlightData& data) {
    // See VarioScreen.cpp for why this is compiled out entirely in
    // release builds rather than left as a DBGLN no-op.
#ifdef DEBUG
    String status = data.gpsFix ? "Settings: GPS fix active" : "Settings: GPS fix pending";
    if (status != lastSettingsStatus) {
        lastSettingsStatus = status;
        DBGLN(status);
    }
#endif

    int line = 3;
    display.display().setCursor(0, 1);
    display.display().print("Settings");
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().print("Fix:");
    display.display().print(data.gpsFix ? "Y" : "N");
    display.display().print("  Sats:");
    display.display().print(static_cast<int>(data.satellites));
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().print("Lat:");
    display.display().print(data.latitude, 3);
    display.display().print(" Lon:");
    display.display().print(data.longitude, 3);
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().print(data.groundSpeed * 3.6f, 1);
    display.display().print("km/h ");
    display.display().print(data.barometricAltitude, 1);
    display.display().print("m");
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().print("Vario:");
    display.display().print(data.verticalSpeed, 2);
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().print("Batt:");
    display.display().print(static_cast<int>(data.batteryPercent));
    display.display().print("% Volts:");
    display.display().print(data.batteryVoltage, 2);
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().print("Wind:");
    display.display().print(data.windSpeed, 1);
    display.display().print("m/s State:");
    display.display().print(static_cast<int>(data.flightState));

    //todo: allow min satellites to be changed in settings
    //todo: allow Alt Trace sampling to be adjusted and ring buffer size to be changed in settings

    if (display.settingsEditMode_) {
        display.display().setCursor(0, line++ * Config::LINE_SPACING);
        display.display().print(">");
    }
}

void SettingsScreen::exit() {}

}  // namespace variometer
