#pragma once

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#include "config/Config.h"
#include "core/FlightData.h"
#include "display/Screen.h"
#include "flight/FlightRecorder.h"
#include "power/PowerManager.h"

namespace variometer {

class SimpleDisplay {
public:
    SimpleDisplay() = default;

    void begin() {
        display_.begin(0x3C, true);
        display_.clearDisplay();
        display_.display();
        display_.setTextSize(1);
        display_.setTextColor(SH110X_WHITE);
    }

    void clear() {
        display_.clearDisplay();
    }

    void setCursor(int16_t x, int16_t y) {
        display_.setCursor(x, y);
    }

    size_t print(const char* text) {
        return display_.print(text);
    }

    size_t print(const String& text) {
        return display_.print(text.c_str());
    }

    size_t print(int value, int base = 10) {
        return display_.print(value, base);
    }

    size_t print(float value, int digits = 2) {
        return display_.print(value, digits);
    }

    size_t println(const char* text) {
        return display_.println(text);
    }

    void setTextSize(uint8_t size) {
        display_.setTextSize(size);
    }

    void setTextColor(uint16_t color) {
        display_.setTextColor(color);
    }

    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
        display_.drawRect(x, y, w, h, color);
    }

    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
        display_.fillRect(x, y, w, h, color);
    }

    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
        display_.drawLine(x0, y0, x1, y1, color);
    }

    void drawPixel(int16_t x, int16_t y, uint16_t color) {
        display_.drawPixel(x, y, color);
    }

    void drawCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {
        display_.drawCircle(x, y, r, color);
    }

    void fillCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {
        display_.fillCircle(x, y, r, color);
    }

    void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                      int16_t x2, int16_t y2, uint16_t color) {
        display_.drawTriangle(x0, y0, x1, y1, x2, y2, color);
    }

    void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                      int16_t x2, int16_t y2, uint16_t color) {
        display_.fillTriangle(x0, y0, x1, y1, x2, y2, color);
    }

    void display() {
        display_.display();
    }

private:
    Adafruit_SH1107 display_{128, 128, &Wire};
};

class StartUpScreen;
class PreTakeoffScreen;
class VarioScreen;
class AltitudeTraceScreen;
class WindDirectionScreen;
class FlightMapScreen;
class FlightTrack;
class SettingsScreen;
class PowerOffScreen;
class LandedScreen;

class DisplayManager {
public:
    DisplayManager();
    void begin();
    void update(const FlightData& data);
    void handleEncoderDelta(int8_t delta);
    void handleButtonPress();
    void enterSettingsEditMode();
    void exitSettingsEditMode();
    void setPowerManager(PowerManager* powerManager);
    void setRecorder(FlightRecorder* recorder);
    void setTrack(FlightTrack* track);
    void setReplaySpeed(uint8_t speed);
    bool settingsEditMode_ = false;
    uint8_t settingsEditIndex_ = 0;
    uint8_t minSatellitesSetting_ = Config::MIN_SATELLITES_DEFAULT;
    bool audioEnabled_ = true;
    bool backgroundWhite_ = false;
    FlightRecorder* recorder() const;
    SimpleDisplay& display();
    bool isPowerOffScreen() const { return activeScreen_ == ScreenId::PowerOff; }
    bool isPreTakeoffScreen() const { return activeScreen_ == ScreenId::PreTakeoff; }

private:
    void updateScreenSelection(const FlightData& data);
    void drawCurrentScreen(const FlightData& data);
    void setScreen(ScreenId screen);

    ScreenId activeScreen_ = ScreenId::StartUp;
    ScreenId lastScreen_ = ScreenId::StartUp;
    FlightState currentFlightState_ = FlightState::PREFLIGHT;
    StartUpScreen* startupScreen_ = nullptr;
    PreTakeoffScreen* preTakeoffScreen_ = nullptr;
    VarioScreen* variometerScreen_ = nullptr;
    AltitudeTraceScreen* altitudeTraceScreen_ = nullptr;
    WindDirectionScreen* windDirectionScreen_ = nullptr;
    FlightMapScreen* flightMapScreen_ = nullptr;
    SettingsScreen* settingsScreen_ = nullptr;
    PowerOffScreen* powerOffScreen_ = nullptr;
    LandedScreen* landedScreen_ = nullptr;
    bool initialized_ = false;
    bool manualSelectionActive_ = false;
    FlightRecorder* recorder_ = nullptr;
    FlightTrack* track_ = nullptr;
    PowerManager* powerManager_ = nullptr;
    SimpleDisplay display_;
};

}  // namespace variometer
