#pragma once

#include <Arduino.h>

namespace variometer {

class MS5611Sensor {
public:
    MS5611Sensor() = default;
    void begin();
    void update();
    bool isValid() const;
    float getPressure() const;
    float getTemperature() const;
    float getAltitude() const;
    float getRelativeAltitude() const;
    float getVerticalSpeed() const;

private:
    enum class State : uint8_t {
        Idle,
        WaitingPressure,
        WaitingTemperature
    };

    void requestConversion(uint8_t command);
    uint32_t readAdc();
    bool readProm();

    bool valid_ = false;
    float pressure_ = 0.0f;
    float temperature_ = 0.0f;
    float altitude_ = 0.0f;
    float relativeAltitude_ = 0.0f;
    float verticalSpeed_ = 0.0f;
    uint16_t prom_[6] = {0, 0, 0, 0, 0, 0};
    uint32_t lastTransitionMs_ = 0;
    State state_ = State::Idle;
    uint32_t pressureRaw_ = 0;
    uint32_t temperatureRaw_ = 0;
    float baseAltitude_ = 0.0f;
    float previousAltitude_ = 0.0f;
    uint32_t previousAltitudeMs_ = 0;
    uint32_t lastDebugMs_ = 0;
    uint8_t address_ = 0x76;
};

}  // namespace variometer
