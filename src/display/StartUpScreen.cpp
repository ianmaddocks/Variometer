#include "display/StartUpScreen.h"

#include <Arduino.h>

#include "display/DisplayManager.h"

namespace variometer {
namespace {
    String lastStartupStatus;
}

void StartUpScreen::enter() {
    lastStartupStatus = "";
    DBGLN("Entering startup screen");
}

void StartUpScreen::update(const FlightData& data) {
    (void)data;
}

void StartUpScreen::draw(DisplayManager& display, const FlightData& data) {
    String status = data.gpsFix ? "Startup screen: GPS ready" : "Startup screen: waiting for GPS lock";
    if (status != lastStartupStatus) {
        lastStartupStatus = status;
        DBGLN(status);
    }

    int line = 3;
    display.display().setCursor(0, 1);
    display.display().print("Vario ");
    display.display().print(VARIOMETER_VERSION);
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    display.display().setCursor(0, line++ * Config::LINE_SPACING);
    #ifdef DEBUG
        display.display().print("Sats:");
        display.display().print(static_cast<int>(data.satellites));
        //display.display().setCursor(0, line += Config::LINE_SPACING);
        display.display().print("  Batt:");
        display.display().print(static_cast<int>(data.batteryPercent));
        display.display().print("%");
        display.display().setCursor(0, line++ * Config::LINE_SPACING);
        display.display().setCursor(0, line++ * Config::LINE_SPACING);
    #endif
    display.display().print("wating for GPS lock");
    display.display().setCursor(0, line++ * Config::LINE_SPACING);

    
    const int16_t barX = 8;
    const int16_t barWidth = 112;
    const int16_t barHeight = 8;
    const int progress = ((data.satellites+1) * barWidth) / ((Config::MIN_SATELLITES_DEFAULT > 0) ? Config::MIN_SATELLITES_DEFAULT+1 : 1);
    const int16_t filledWidth = static_cast<int16_t>(progress < barWidth ? progress : barWidth);

    display.display().drawRect(barX, line * Config::LINE_SPACING, barWidth, barHeight, SH110X_WHITE);
    display.display().fillRect(barX, line * Config::LINE_SPACING, filledWidth, barHeight, SH110X_WHITE);
}

void StartUpScreen::exit() {}

}  // namespace variometer
