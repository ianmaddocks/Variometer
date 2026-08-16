#include "display/DisplayManager.h"

#include <Arduino.h>

#include "display/AltitudeTraceScreen.h"
#include "display/LandedScreen.h"
#include "display/PreTakeoffScreen.h"
#include "display/SettingsScreen.h"
#include "display/StartUpScreen.h"
#include "display/VarioScreen.h"
#include "display/WindDirectionScreen.h"
#include "display/PowerOffScreen.h"

namespace variometer {
namespace {
constexpr uint32_t kLogIntervalMs = 1000;
constexpr int16_t kStatusY = 12;

const char* stateName(FlightState state) {
    switch (state) {
        case FlightState::PREFLIGHT:
            return "PREFLIGHT";
        case FlightState::TAKEOFF_DETECTED:
            return "TAKEOFF";
        case FlightState::FLIGHT:
            return "FLIGHT";
        case FlightState::LANDING_DETECTED:
            return "LANDING";
        case FlightState::POST_FLIGHT:
            return "POST";
    }
    return "UNKNOWN";
}

void drawBatteryIcon(SimpleDisplay& display, float percent, int16_t x, int16_t y) {
    const int16_t fillWidth = static_cast<int16_t>(map(static_cast<int>(percent), 0, 100, 0, 13));
    display.drawRect(x, y, 18, 9, SH110X_WHITE);
    display.fillRect(x + 18, y + 2, 2, 5, SH110X_WHITE);
    display.fillRect(x + 2, y + 2, fillWidth, 5, SH110X_WHITE);
}

void drawSignalBars(SimpleDisplay& display, uint8_t satellites, int16_t x, int16_t y) {
    const uint8_t bars = (satellites > Config::MIN_SATELLITES_DEFAULT) ? Config::MIN_SATELLITES_DEFAULT : max(satellites, static_cast<uint8_t>(1));
    for (uint8_t i = 0; i < Config::MIN_SATELLITES_DEFAULT; ++i) {
        const int16_t barHeight = 2 + (i * 6) / (Config::MIN_SATELLITES_DEFAULT - 1); // the first bar is always 2, the last bar reaches 8, intermediate bars scale smoothly between them, the growth is proportional to the bar’s position within the threshold
        const int16_t barY = y - barHeight + 2;
        const int16_t barX = x + i * 3;
        const int16_t barWidth = 2;
        if (i < bars) {
            display.drawRect(barX, barY, barWidth, barHeight, SH110X_WHITE);
        }
    }
}

void drawCommonStatusBar(SimpleDisplay& display, const FlightData& data) {
    drawSignalBars(display, data.satellites, 106-(Config::MIN_SATELLITES_DEFAULT*3), kStatusY-6);
    drawBatteryIcon(display, data.batteryPercent, 108, kStatusY-12);
    display.drawLine(0, 11, 128, 11, SH110X_WHITE);
#ifdef DEBUG
    display.drawLine(0, 128-18, 128, 128-18, SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 128-15);
    display.print("F:");
    display.print(static_cast<int>(data.flightDuration / 60));
    display.print("m");
    display.print(" LZ:");
    display.print(data.distanceFromLZ, 1);
    display.print("km");
    display.print(" S:");
    display.print(static_cast<int>(data.groundSpeed * 3.6f));
    display.print(" A:");
    display.print(static_cast<int>(data.barometricAltitude));
    display.print(" V:");
    display.print(data.verticalSpeed, 1);
    //display.setCursor(95, kStatusY);
#endif
}

}  // namespace

DisplayManager::DisplayManager()
    : startupScreen_(new StartUpScreen()),
      preTakeoffScreen_(new PreTakeoffScreen()),
      variometerScreen_(new VarioScreen()),
      altitudeTraceScreen_(new AltitudeTraceScreen()),
      windDirectionScreen_(new WindDirectionScreen()),
      settingsScreen_(new SettingsScreen()),
      powerOffScreen_(new PowerOffScreen()),
      landedScreen_(new LandedScreen()) {}

SimpleDisplay& DisplayManager::display() {
    return display_;
}

FlightRecorder* DisplayManager::recorder() const {
    return recorder_;
}

void DisplayManager::begin() {
    display_.begin();
    initialized_ = true;
    startupScreen_->enter();
    DBGLN("Display initialized");
}

void DisplayManager::setScreen(ScreenId screen) {
    if (activeScreen_ == screen) {
        return;
    }

    const ScreenId previous = activeScreen_;
    activeScreen_ = screen;

    if (previous == ScreenId::StartUp) startupScreen_->exit();
    if (previous == ScreenId::PreTakeoff) preTakeoffScreen_->exit();
    if (previous == ScreenId::Variometer) variometerScreen_->exit();
    if (previous == ScreenId::AltitudeTrace) altitudeTraceScreen_->exit();
    if (previous == ScreenId::WindDirection) windDirectionScreen_->exit();
    if (previous == ScreenId::Settings) settingsScreen_->exit();
    if (previous == ScreenId::PowerOff) powerOffScreen_->exit();
    if (previous == ScreenId::Landed) landedScreen_->exit();

    if (screen == ScreenId::StartUp) startupScreen_->enter();
    if (screen == ScreenId::PreTakeoff) preTakeoffScreen_->enter();
    if (screen == ScreenId::Variometer) variometerScreen_->enter();
    if (screen == ScreenId::AltitudeTrace) altitudeTraceScreen_->enter();
    if (screen == ScreenId::WindDirection) windDirectionScreen_->enter();
    if (screen == ScreenId::Settings) settingsScreen_->enter();
    if (screen == ScreenId::PowerOff) powerOffScreen_->enter();
    if (screen == ScreenId::Landed) landedScreen_->enter();
}

void DisplayManager::updateScreenSelection(const FlightData& data) {
    if (!initialized_) {
        return;
    }

    const FlightState previousState = currentFlightState_;
    currentFlightState_ = data.flightState;

    if (data.flightState == FlightState::FLIGHT || data.flightState == FlightState::TAKEOFF_DETECTED) {
        const bool enteringFlight = previousState != FlightState::FLIGHT &&
                                    previousState != FlightState::TAKEOFF_DETECTED;
        if (enteringFlight) {
            manualSelectionActive_ = false;
            setScreen(ScreenId::Variometer);
        } else if (!manualSelectionActive_ && activeScreen_ != ScreenId::Variometer) {
            setScreen(ScreenId::Variometer);
        }
        return;
    }

    if (data.flightState == FlightState::POST_FLIGHT || data.flightState == FlightState::LANDING_DETECTED) {
        const bool enteringPostFlight = previousState != FlightState::POST_FLIGHT &&
                                        previousState != FlightState::LANDING_DETECTED;
        if (enteringPostFlight) {
            manualSelectionActive_ = false;
            setScreen(ScreenId::Landed);
        } else if (!manualSelectionActive_ && activeScreen_ != ScreenId::Landed) {
            setScreen(ScreenId::Landed);
        }
        return;
    }

    if (manualSelectionActive_) {
        return;
    }

    if (data.gpsFix && data.satellites >= 4) {
        if (activeScreen_ == ScreenId::StartUp) {
            setScreen(ScreenId::PreTakeoff);
        }
    } else if (activeScreen_ != ScreenId::StartUp) {
        setScreen(ScreenId::StartUp);
    }
}

void DisplayManager::drawCurrentScreen(const FlightData& data) {
    if (!initialized_) {
        return;
    }

    if (activeScreen_ != lastScreen_) {
        lastScreen_ = activeScreen_;
        DBGF("Screen transition -> %d\n", static_cast<int>(activeScreen_));
    }

    display_.clear();

    if (activeScreen_ == ScreenId::Variometer && recorder_ != nullptr && recorder_->size() > 1) {
        const size_t count = recorder_->size();
        const TracePoint& first = recorder_->at(0);
        const TracePoint& latest = recorder_->at(count - 1);
        const float span = latest.altitude - first.altitude;
        //Serial.printf("Trace points=%u span=%.1f\n", static_cast<unsigned>(count), span);
    }

    switch (activeScreen_) {
        case ScreenId::StartUp:
            startupScreen_->draw(*this, data);
            break;
        case ScreenId::PreTakeoff:
            preTakeoffScreen_->draw(*this, data);
            break;
        case ScreenId::Variometer:
            variometerScreen_->draw(*this, data);
            break;
        case ScreenId::AltitudeTrace:
            altitudeTraceScreen_->draw(*this, data);
            break;
        case ScreenId::WindDirection:
            windDirectionScreen_->draw(*this, data);
            break;
        case ScreenId::Settings:
            settingsScreen_->draw(*this, data);
            break;
        case ScreenId::PowerOff:
            powerOffScreen_->draw(*this, data);
            break;
        case ScreenId::Landed:
            landedScreen_->draw(*this, data);
            break;
    }

    drawCommonStatusBar(display_, data);
    display_.display();
}

void DisplayManager::handleEncoderDelta(int8_t delta) {
    if (delta == 0) {
        return;
    }

    DBGF("DisplayManager: encoder delta=%d currentScreen=%d\n", delta, static_cast<int>(activeScreen_));

    if (activeScreen_ == ScreenId::Settings) {
        if (settingsEditMode_) {
            if (settingsEditIndex_ == 0) {
                if (delta > 0) {
                    if (minSatellitesSetting_ < 10) {
                        ++minSatellitesSetting_;
                    }
                } else if (minSatellitesSetting_ > 0) {
                    --minSatellitesSetting_;
                }
            } else if (settingsEditIndex_ == 1) {
                audioEnabled_ = !audioEnabled_;
            } else {
                backgroundWhite_ = !backgroundWhite_;
            }
            DBGF("Settings edit item=%u value=%u\n", static_cast<unsigned>(settingsEditIndex_),
                 static_cast<unsigned>(settingsEditIndex_ == 0 ? minSatellitesSetting_ : (audioEnabled_ ? 1 : 0)));
            return;
        }
    }

    manualSelectionActive_ = true;
    lastScreen_ = activeScreen_;

    ScreenId nextScreen = activeScreen_;

    if (currentFlightState_ == FlightState::PREFLIGHT) {
        static const ScreenId preflightOrder[] = {ScreenId::PreTakeoff, ScreenId::Settings, ScreenId::PowerOff};
        constexpr int count = 3;
        for (int i = 0; i < count; ++i) {
            if (preflightOrder[i] == activeScreen_) {
                const int nextIndex = (i + delta + count) % count;
                nextScreen = preflightOrder[nextIndex];
                break;
            }
        }
    } else if (currentFlightState_ == FlightState::FLIGHT || currentFlightState_ == FlightState::TAKEOFF_DETECTED) {
        static const ScreenId inFlightOrder[] = {ScreenId::Variometer, ScreenId::AltitudeTrace, ScreenId::WindDirection, ScreenId::Settings};
        constexpr int count = 4;
        for (int i = 0; i < count; ++i) {
            if (inFlightOrder[i] == activeScreen_) {
                const int nextIndex = (i + delta + count) % count;
                nextScreen = inFlightOrder[nextIndex];
                break;
            }
        }
    } else if (currentFlightState_ == FlightState::LANDING_DETECTED || currentFlightState_ == FlightState::POST_FLIGHT) {
        static const ScreenId landedOrder[] = {ScreenId::Landed, ScreenId::AltitudeTrace, ScreenId::Settings, ScreenId::PowerOff};
        constexpr int count = 4;
        for (int i = 0; i < count; ++i) {
            if (landedOrder[i] == activeScreen_) {
                const int nextIndex = (i + delta + count) % count;
                nextScreen = landedOrder[nextIndex];
                break;
            }
        }
    }

    setScreen(nextScreen);
    DBGF("Screen changed via encoder to %d\n", static_cast<int>(nextScreen));
}

void DisplayManager::handleButtonPress() {
    if (activeScreen_ == ScreenId::Settings) {
        if (!settingsEditMode_) {
            enterSettingsEditMode();
        } else {
            exitSettingsEditMode();
        }
        return;
    }

    if (activeScreen_ == ScreenId::PowerOff) {
        DBGLN("Power-off screen active: long press required to shutdown");
        return;
    }

    DBGLN("Button press acknowledged by display manager");
}

void DisplayManager::enterSettingsEditMode() {
    settingsEditMode_ = true;
    settingsEditIndex_ = 0;
    DBGLN("Settings edit mode enabled");
}

void DisplayManager::exitSettingsEditMode() {
    settingsEditMode_ = false;
    settingsEditIndex_ = 0;
    DBGLN("Settings edit mode disabled");
}

void DisplayManager::setPowerManager(PowerManager* powerManager) {
    powerManager_ = powerManager;
}

void DisplayManager::setRecorder(FlightRecorder* recorder) {
    recorder_ = recorder;
}

void DisplayManager::update(const FlightData& data) {
    static uint32_t lastLogMs = 0;
    const uint32_t now = millis();

    updateScreenSelection(data);
    drawCurrentScreen(data);

    if (now - lastLogMs >= kLogIntervalMs) {
        DBGF("State=%s screen=%d sats=%u speed=%.1fkm/h vario=%.2fm/s batt=%.0f%%\n",
             stateName(data.flightState),
             static_cast<int>(activeScreen_),
             static_cast<unsigned>(data.satellites),
             data.groundSpeed * 3.6f,
             data.verticalSpeed,
             data.batteryPercent);
        lastLogMs = now;
    }
}

}  // namespace variometer
