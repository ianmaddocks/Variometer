#include "config/config.h"
#include "display/DisplayManager.h"

#include <Arduino.h>

#include "display/AltitudeTraceScreen.h"
#include "display/LandedScreen.h"
#include "display/SettingsScreen.h"
#include "display/VarioBarScreen.h"
#include "display/WindDirectionScreen.h"
#include "display/FlightMapScreen.h"

namespace variometer {
namespace {
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

void drawRecordingIndicator(SimpleDisplay& display, const FlightData& data) {
    // Blinking dot + mm:ss, replacing the signal-bar/battery corner while
    // a manual SW1 capture is running -- those two are the least useful
    // thing to see mid-recording, and every screen already uses x<70 for
    // its own header text (see the setCursor(0, 1) calls across the
    // Screen implementations), so this is the one collision-free spot.
    char buf[16];
    const uint32_t s = data.recordingDurationS;
    snprintf(buf, sizeof(buf), "REC %lu:%02lu",
             static_cast<unsigned long>(s / 60), static_cast<unsigned long>(s % 60));
    display.setTextSize(1);
    display.setCursor(70, 2);
    display.print(buf);
    if ((millis() / 500) % 2 == 0) {
        display.fillCircle(66, 5, 2, SH110X_WHITE);
    }
}

void drawCommonStatusBar(SimpleDisplay& display, const FlightData& data, ScreenId activeScreen) {
    if (data.recordingActive) {
        drawRecordingIndicator(display, data);
    } else {
        drawSignalBars(display, data.satellites, 106-(Config::MIN_SATELLITES_DEFAULT*3), kStatusY-6);
        drawBatteryIcon(display, data.batteryPercent, 108, kStatusY-12);
    }
    display.drawLine(0, 11, 128, 11, SH110X_WHITE);
#ifdef DEBUG
    /*
     * VarioBar owns the bottom third of the screen for its own footer
     * (dotted rule at y=94 down through its value row at y=124), unlike
     * every other screen here, which -- like WindDirectionScreen's
     * kContentBottom -- leaves y>=110 clear for this debug strip. Drawing
     * both meant this strip's line/text landed directly on top of
     * VarioBarScreen's footer labels and values, e.g. this strip's "LZ:"
     * printing right through VarioBarScreen's "AGL"/altitude figures.
     * VarioBarScreen's own footer already surfaces altitude and a
     * selectable field, so this strip is redundant there, not lost.
     */
    if (activeScreen == ScreenId::VarioBar) {
        return;
    }

    display.drawLine(0, 128-9, 128, 128-9, SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 128-7);
    display.print("F:");
    display.print(static_cast<int>(data.flightDuration / 60));
    display.print("m");
    display.print(" LZ:");
    display.print(data.distanceFromLZ, 0);
    display.print("km");
    display.print(" S:");
    display.print(static_cast<int>(data.groundSpeed * 3.6f));
    display.print("km/h");
    //display.print(" A:");
    //display.print(static_cast<int>(data.barometricAltitude));
    //display.print(" V:");
    //display.print(data.verticalSpeed, 1);
    //display.setCursor(95, kStatusY);
#endif
}

}  // namespace

DisplayManager::DisplayManager()
    : varioBarScreen_(new VarioBarScreen()),
      altitudeTraceScreen_(new AltitudeTraceScreen()),
      windDirectionScreen_(new WindDirectionScreen()),
      flightMapScreen_(new FlightMapScreen()),
      settingsScreen_(new SettingsScreen()),
      landedScreen_(new LandedScreen()) {}

SimpleDisplay& DisplayManager::display() {
    return display_;
}

FlightRecorder* DisplayManager::recorder() const {
    return recorder_;
}

void DisplayManager::setTrack(FlightTrack* track) {
    track_ = track;
    flightMapScreen_->setTrack(track);
}

void DisplayManager::setReplaySpeed(uint8_t speed) {
    flightMapScreen_->setReplaySpeed(speed);
}

void DisplayManager::begin() {
    display_.begin();
    initialized_ = true;
    varioBarScreen_->enter();
    DBGLN("Display initialized");
}

void DisplayManager::setScreen(ScreenId screen) {
    if (activeScreen_ == screen) {
        return;
    }

    const ScreenId previous = activeScreen_;
    activeScreen_ = screen;

    if (previous == ScreenId::VarioBar) varioBarScreen_->exit();
    if (previous == ScreenId::AltitudeTrace) altitudeTraceScreen_->exit();
    if (previous == ScreenId::WindDirection) windDirectionScreen_->exit();
    if (previous == ScreenId::FlightMap) flightMapScreen_->exit();
    if (previous == ScreenId::Settings) settingsScreen_->exit();
    if (previous == ScreenId::Landed) landedScreen_->exit();

    if (screen == ScreenId::VarioBar) varioBarScreen_->enter();
    if (screen == ScreenId::AltitudeTrace) altitudeTraceScreen_->enter();
    if (screen == ScreenId::WindDirection) windDirectionScreen_->enter();
    if (screen == ScreenId::FlightMap) flightMapScreen_->enter();
    if (screen == ScreenId::Settings) settingsScreen_->enter();
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
            setScreen(ScreenId::VarioBar);
        } else if (!manualSelectionActive_ && activeScreen_ != ScreenId::VarioBar) {
            setScreen(ScreenId::VarioBar);
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

    // PREFLIGHT: no forced screen. VarioBar is the default/initial screen
    // and stays put until the pilot manually cycles elsewhere.
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

    if (isPoweringOff()) {
        // Shown for the ~800ms the power-off tune plays before radios
        // are stopped and the screen goes blank ahead of deep sleep --
        // see PowerManager::readyToSleep() / Application::loop(). Drawn
        // as a full-screen overlay rather than a dedicated screen so it
        // can interrupt whatever screen was active at the time.
        display_.setCursor(0, 1);
        display_.print("Powering Off");
        display_.setCursor(0, Config::LINE_SPACING);
        display_.print("Please wait...");
        display_.display();
        return;
    }

    if (activeScreen_ == ScreenId::VarioBar && recorder_ != nullptr && recorder_->size() > 1) {
        const size_t count = recorder_->size();
        const TracePoint& first = recorder_->at(0);
        const TracePoint& latest = recorder_->at(count - 1);
        const float span = latest.altitude - first.altitude;
        //Serial.printf("Trace points=%u span=%.1f\n", static_cast<unsigned>(count), span);
    }

    switch (activeScreen_) {
        case ScreenId::VarioBar:
            varioBarScreen_->draw(*this, data);
            break;
        case ScreenId::AltitudeTrace:
            altitudeTraceScreen_->draw(*this, data);
            break;
        case ScreenId::WindDirection:
            windDirectionScreen_->draw(*this, data);
            break;
        case ScreenId::FlightMap:
            // update() advances the zoom staging and replay clock; both
            // must run at the display rate, independent of GPS updates.
            flightMapScreen_->update(data);
            flightMapScreen_->draw(*this, data);
            break;
        case ScreenId::Settings:
            settingsScreen_->draw(*this, data);
            break;
        case ScreenId::Landed:
            landedScreen_->draw(*this, data);
            break;
    }

    drawCommonStatusBar(display_, data, activeScreen_);
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

    /*
     * In the 3D replay the encoder orbits the view rather than changing
     * screen, so the rotation is consumed here. The pilot leaves 3D with
     * a button press, which keeps one control doing one thing per mode.
     */
    if (activeScreen_ == ScreenId::FlightMap && flightMapScreen_->isThreeD()) {
        flightMapScreen_->rotateView(delta);
        return;
    }

    manualSelectionActive_ = true;
    lastScreen_ = activeScreen_;

    ScreenId nextScreen = activeScreen_;

    if (currentFlightState_ == FlightState::PREFLIGHT) {
        // VarioBar is the default preflight screen; the only other screen
        // reachable from it before takeoff is Settings.
        static const ScreenId preflightOrder[] = {ScreenId::VarioBar, ScreenId::Settings};
        constexpr int count = 2;
        for (int i = 0; i < count; ++i) {
            if (preflightOrder[i] == activeScreen_) {
                const int nextIndex = (i + delta + count) % count;
                nextScreen = preflightOrder[nextIndex];
                break;
            }
        }
    } else if (currentFlightState_ == FlightState::FLIGHT || currentFlightState_ == FlightState::TAKEOFF_DETECTED) {
        static const ScreenId inFlightOrder[] = {ScreenId::VarioBar,
                                                 ScreenId::WindDirection, ScreenId::FlightMap,
                                                 ScreenId::AltitudeTrace, ScreenId::Settings};
        constexpr int count = 5;
        for (int i = 0; i < count; ++i) {
            if (inFlightOrder[i] == activeScreen_) {
                const int nextIndex = (i + delta + count) % count;
                nextScreen = inFlightOrder[nextIndex];
                break;
            }
        }
    } else if (currentFlightState_ == FlightState::LANDING_DETECTED || currentFlightState_ == FlightState::POST_FLIGHT) {
        static const ScreenId landedOrder[] = {ScreenId::Landed, ScreenId::FlightMap,
                                               ScreenId::AltitudeTrace, ScreenId::Settings};
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

    /*
     * On the map, a press switches between the plan view and the
     * post-flight 3D replay. The screen itself refuses the change while
     * airborne, so this stays a single unconditional gesture.
     */
    if (activeScreen_ == ScreenId::FlightMap) {
        flightMapScreen_->toggleViewMode(currentFlightState_);
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

void DisplayManager::blankScreen() {
    display_.clear();
    display_.display();
}

void DisplayManager::setRecorder(FlightRecorder* recorder) {
    recorder_ = recorder;
}

void DisplayManager::update(const FlightData& data) {
    static uint32_t lastLogMs = 0;
    const uint32_t now = millis();

    updateScreenSelection(data);
    drawCurrentScreen(data);

    if (now - lastLogMs >= Config::STATE_LOG_INTERVAL_MS) {
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
