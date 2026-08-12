#include "display/SettingsScreen.h"

#include <Arduino.h>

#include "display/DisplayManager.h"

namespace variometer {
namespace {
    String lastSettingsStatus;
}

void SettingsScreen::enter() {
    lastSettingsStatus = "";
    Serial.println("Entering settings screen");
}

void SettingsScreen::update(const FlightData& data) {
    (void)data;
}

void SettingsScreen::draw(DisplayManager& display, const FlightData& data) {
    String status = data.gpsFix ? "Settings: GPS fix active" : "Settings: GPS fix pending";
    if (status != lastSettingsStatus) {
        lastSettingsStatus = status;
        Serial.println(status);
    }

    int line = 5;
    display.display().setCursor(0, line += Config::LINE_SPACING);
    display.display().print("Settings");
    display.display().setCursor(0, line += Config::LINE_SPACING);
    display.display().print("GPS");
    display.display().setCursor(0, line += Config::LINE_SPACING);
    display.display().print("Fix:");
    display.display().print(data.gpsFix ? "Y" : "N");
    display.display().print("  Sats:");
    display.display().print(static_cast<int>(data.satellites));
    display.display().setCursor(0, line += Config::LINE_SPACING);
    display.display().print("Lat:");
    display.display().print(data.latitude, 4);
    display.display().setCursor(0, line += Config::LINE_SPACING);
    display.display().print("Lon:");
    display.display().print(data.longitude, 4);
    display.display().setCursor(0, line += Config::LINE_SPACING);
    display.display().print("Spd:");
    display.display().print(data.groundSpeed * 3.6f, 1);
    display.display().print("km/h");
    display.display().setCursor(0, line += Config::LINE_SPACING);
    display.display().print("MS5611");
    display.display().setCursor(0, line += Config::LINE_SPACING);
    display.display().print("Alt:");
    display.display().print(data.barometricAltitude, 1);
    display.display().print("m  Vario:");
    display.display().print(data.verticalSpeed, 2);
    display.display().setCursor(0, line += Config::LINE_SPACING);
    display.display().print("Batt:");
    display.display().print(static_cast<int>(data.batteryPercent));
    display.display().print("%  Wind:");
    display.display().print(data.windSpeed, 1);
    display.display().print("m/s");
    display.display().setCursor(0, line += Config::LINE_SPACING);
    display.display().print("State:");
    display.display().print(static_cast<int>(data.flightState));

    if (display.settingsEditMode_) {
        display.display().setCursor(0, 16 + (display.settingsEditIndex_ * Config::LINE_SPACING));
        display.display().print(">");
    }
}

void SettingsScreen::exit() {}

}  // namespace variometer
