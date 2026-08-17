#pragma once

#include <Arduino.h>
//#define USE_MOCK_MS5611 1
#ifdef USE_MOCK_MS5611
#include "MockMS5611.h"
#endif

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

    // Sets the current pressure as the zero-altitude reference.
    void setTakeoffReference();

private:
    enum class State : uint8_t {
        WaitingPressure,
        WaitingTemperature
    };

    void requestConversion(uint8_t command);
    uint32_t readAdc();
    bool readProm();
    void processMeasurements();
    float pressureToAltitude(float pressurePa) const;

    bool valid_ = false;

    float pressure_ = 0.0f;
    float temperature_ = 0.0f;
    float altitude_ = 0.0f;
    float relativeAltitude_ = 0.0f;

    // MS5611 calibration coefficients C1..C6.
    uint16_t prom_[6] = {0, 0, 0, 0, 0, 0};

    uint32_t pressureRaw_ = 0;
    uint32_t temperatureRaw_ = 0;

    uint32_t lastTransitionMs_ = 0;
    uint32_t lastDebugMs_ = 0;

    State state_ = State::WaitingPressure;

    // Pressure at takeoff/reference point.
    float referencePressurePa_ = 101325.0f;
    bool referenceSet_ = false;

    uint8_t address_ = 0x76;
#ifdef USE_MOCK_MS5611
    MockMS5611 mockMS5611_;
#endif
};

}  // namespace variometer