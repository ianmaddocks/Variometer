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

    /*
     * Sample handshake.
     *
     * The sensor produces a new reading far less often than callers poll
     * it, so consumers need a way to tell a genuinely new measurement
     * from a repeat of the last one. getSampleSequence() increments once
     * per accepted measurement; compare it against the value you last
     * saw to detect a fresh sample.
     *
     * getSampleTimeMs() is the millis() value at the moment the
     * measurement completed -- NOT the time it was read out. Feeding the
     * readout time into the vario regression added up to a full
     * measurement period of timing error to every sample.
     */
    uint32_t getSampleSequence() const;
    uint32_t getSampleTimeMs() const;

    // False means the calibration PROM failed its own CRC, so every
    // derived pressure/altitude is untrustworthy. Suitable for the
    // "Sensor status" line on the Settings screen.
    bool hasValidPromCrc() const;

    // Last raw ADC conversions, for diagnostics.
    uint32_t getRawPressure() const;
    uint32_t getRawTemperature() const;

    /*
     * Health counters.
     *
     * Failures are counted rather than logged individually. Each failure
     * previously emitted three serial lines (~120 bytes), which at
     * 115200 baud blocks for ~10 ms -- so logging the faults was itself
     * stalling the loop it was trying to diagnose.
     *
     * Call resetCounters() after reporting to get per-interval rates.
     */
    struct Counters {
        uint32_t samples;         // measurements accepted
        uint32_t adcFailPressure; // D1 read returned zero
        uint32_t adcFailTemp;     // D2 read returned zero
        uint32_t convertFail;     // convert command not delivered
        uint32_t readFail;        // ADC read transaction itself failed
        uint32_t rangeReject;     // computed value outside sane limits
        uint32_t tempReject;      // D2 corrupt: temperature implausible
    };

    const Counters& getCounters() const;
    void resetCounters();

    // I2C error code from the most recent failure (Wire semantics:
    // 2 = address NACK, 3 = data NACK, 4 = other, 5 = timeout).
    uint8_t getLastI2cError() const;

    // Sets the current pressure as the zero-altitude reference.
    void setTakeoffReference();

private:
    enum class State : uint8_t {
        WaitingPressure,
        WaitingTemperature
    };

    // Returns false if the command could not be delivered, in which case
    // no conversion is running and a later ADC read will return zero.
    bool requestConversion(uint8_t command);
    uint32_t readAdc();
    bool readProm();

    /*
     * MS5611 PROM CRC4, per the datasheet / AN520.
     *
     * The definitive test for whether the calibration coefficients were
     * read correctly. Plausible-looking temperature with a wildly wrong
     * pressure is exactly what a partly-corrupt PROM looks like, so this
     * distinguishes a calibration problem from a conversion problem.
     */
    static uint8_t crc4(uint16_t words[8]);

    void processMeasurements();
    float pressureToAltitude(float pressurePa) const;

    bool valid_ = false;

    float pressure_ = 0.0f;
    float temperature_ = 0.0f;
    float altitude_ = 0.0f;
    float relativeAltitude_ = 0.0f;

    /*
     * All eight PROM words, indexed by address offset so the layout
     * matches the datasheet directly:
     *
     *   [0] 0xA0  factory data / CRC nibble
     *   [1] 0xA2  C1 .. [6] 0xAC  C6
     *   [7] 0xAE  serial code, low 4 bits hold the CRC
     *
     * Previously only C1..C6 were read, which left no way to verify the
     * calibration data had arrived intact.
     */
    uint16_t promWords_[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    bool promCrcValid_ = false;

    Counters counters_{};

    // Last I2C error seen, kept so the health report can name the fault
    // without logging every occurrence.
    uint8_t lastI2cError_ = 0;

    uint32_t pressureRaw_ = 0;
    uint32_t temperatureRaw_ = 0;

    uint32_t lastTransitionMs_ = 0;
    uint32_t lastDebugMs_ = 0;

    // Incremented once per accepted measurement; see getSampleSequence().
    uint32_t sampleSequence_ = 0;
    uint32_t sampleTimeMs_ = 0;

    State state_ = State::WaitingPressure;

    // Pressure at takeoff/reference point.
    float referencePressurePa_ = 101325.0f;

    /*
     * Altitude equivalent of referencePressurePa_, cached because the
     * ESP32-C3 is RV32IMC and has no hardware FPU -- every powf() is
     * software-emulated. Recomputing the (unchanging) reference altitude
     * on every sample cost two of the three powf() calls per reading.
     */
    float referenceAltitude_ = 0.0f;
    bool referenceSet_ = false;

    uint8_t address_ = 0x76;
#ifdef USE_MOCK_MS5611
    MockMS5611 mockMS5611_;
#endif
};

}  // namespace variometer