#pragma once

#include <Arduino.h>

namespace variometer {

/*
 * Barometric altitude and temperature sensor interface.
 *
 * The implementation is kept in the driver source so the rest of the
 * application does not depend on the physical sensor model.
 */
class BiometricSensor {
public:
    BiometricSensor() = default;

    void begin();
    void update();

    bool isValid() const;

    float getPressure() const;
    float getTemperature() const;
    float getAltitude() const;
    float getRelativeAltitude() const;

    uint32_t getSampleSequence() const;
    uint32_t getSampleTimeMs() const;

    bool hasValidCalibration() const;

    int32_t getRawPressure() const;
    int32_t getRawTemperature() const;

    struct Counters {
        uint32_t samples;
        uint32_t adcFailPressure;
        uint32_t adcFailTemp;
        uint32_t convertFail;
        uint32_t readFail;
        uint32_t rangeReject;
        uint32_t tempReject;
    };

    const Counters& getCounters() const;
    void resetCounters();

    uint8_t getLastI2cError() const;
    void setTakeoffReference();

private:
    enum class State : uint8_t {
        WaitingPressure,
        WaitingTemperature
    };

    bool writeRegister(uint8_t reg, uint8_t value);
    bool readRegister(uint8_t reg, uint8_t& value);
    bool readRegisters(uint8_t reg, uint8_t* buffer, uint8_t count);

    bool applyTemperatureFix();
    bool primeTemperature();
    bool readCalibrationCoefficients();
    void processMeasurements();
    float pressureToAltitude(float pressurePa) const;

    bool valid_ = false;
    float pressure_ = 0.0f;
    float temperature_ = 0.0f;
    float altitude_ = 0.0f;
    float relativeAltitude_ = 0.0f;

    int32_t c0_ = 0;
    int32_t c1_ = 0;
    int32_t c00_ = 0;
    int32_t c10_ = 0;
    int32_t c01_ = 0;
    int32_t c11_ = 0;
    int32_t c20_ = 0;
    int32_t c21_ = 0;
    int32_t c30_ = 0;
    bool calibrationValid_ = false;

    Counters counters_{};
    uint8_t lastI2cError_ = 0;
    int32_t pressureRaw_ = 0;
    int32_t temperatureRaw_ = 0;
    uint32_t lastTransitionMs_ = 0;
    uint32_t lastDebugMs_ = 0;
    uint32_t sampleSequence_ = 0;
    uint32_t sampleTimeMs_ = 0;
    State state_ = State::WaitingPressure;
    float referencePressurePa_ = 101325.0f;
    float referenceAltitude_ = 0.0f;
    bool referenceSet_ = false;
    uint8_t address_ = 0x77;
    bool tempSensorExternal_ = true;
};

}  // namespace variometer