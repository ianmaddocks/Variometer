#include "sensors/MS5611Sensor.h"

#include <math.h>
#include <Wire.h>

#include "config/Config.h"

namespace variometer {
namespace {

constexpr uint8_t kAddressPrimary = 0x76;
constexpr uint8_t kAddressSecondary = 0x77;

constexpr uint8_t kResetCommand = 0x1E;

// MS5611 PROM calibration coefficients:
// C1 = 0xA2
// C2 = 0xA4
// C3 = 0xA6
// C4 = 0xA8
// C5 = 0xAA
// C6 = 0xAC
constexpr uint8_t kPromBase = 0xA2;
constexpr uint8_t kReadAdc = 0x00;

// OSR 4096.
constexpr uint8_t kConvertD1 = 0x48;
constexpr uint8_t kConvertD2 = 0x58;

// OSR 4096 conversion time is approximately 9.04 ms.
// We allow 12 ms before reading the result.
constexpr uint32_t kConversionTimeMs = 10;

constexpr float kSeaLevelPressurePa = 101325.0f;
constexpr float kAltitudeExponent = 0.190294957f;

}  // namespace

bool MS5611Sensor::requestConversion(uint8_t command) {
    Wire.beginTransmission(address_);
    Wire.write(command);

    /*
     * The result of this write used to be discarded, which hid the most
     * likely cause of "ADC read returned zero": if the convert command
     * never reaches the chip there is no conversion in progress, so the
     * subsequent ADC read legitimately returns 0x000000. The read
     * transaction succeeds, so nothing else in the driver looked wrong.
     *
     * A non-zero code here points at the bus rather than the sensor --
     * see Config::I2C_CLOCK_HZ if these appear in numbers.
     */
    const uint8_t error = Wire.endTransmission();

    if (error != 0) {
        ++counters_.convertFail;
        lastI2cError_ = error;
        return false;
    }

    return true;
}

uint8_t MS5611Sensor::crc4(uint16_t words[8]) {
    uint16_t remainder = 0;

    // The CRC nibble itself is excluded from the calculation.
    const uint16_t originalZero = words[0];
    const uint16_t originalSeven = words[7];

    words[0] = words[0] & 0x0FFF;
    words[7] = 0;

    for (uint8_t byteIndex = 0; byteIndex < 16; ++byteIndex) {
        if ((byteIndex % 2) == 1) {
            remainder ^= static_cast<uint16_t>(words[byteIndex >> 1] & 0x00FF);
        } else {
            remainder ^= static_cast<uint16_t>(words[byteIndex >> 1] >> 8);
        }

        for (uint8_t bit = 8; bit > 0; --bit) {
            if (remainder & 0x8000) {
                remainder = static_cast<uint16_t>((remainder << 1) ^ 0x3000);
            } else {
                remainder = static_cast<uint16_t>(remainder << 1);
            }
        }
    }

    words[0] = originalZero;
    words[7] = originalSeven;

    return static_cast<uint8_t>((remainder >> 12) & 0x000F);
}

uint32_t MS5611Sensor::readAdc() {
    Wire.beginTransmission(address_);
    Wire.write(kReadAdc);

    const uint8_t error = Wire.endTransmission();

    if (error != 0) {
        ++counters_.readFail;
        lastI2cError_ = error;
        return 0;
    }

    const uint8_t requested = 3;
    const uint8_t received = Wire.requestFrom(address_, requested);

    if (received != requested) {
        ++counters_.readFail;
        return 0;
    }

#ifdef USE_MOCK_MS5611
    //DBG( "Read ADC successfully\n");
    uint8_t b0, b1, b2;
    mockMS5611_.readADC(b0, b1, b2);
    const uint32_t rv = (static_cast<uint32_t>(b0) << 16) |
                        (static_cast<uint32_t>(b1) << 8) |
                        (static_cast<uint32_t>(b2));
#else
    const uint8_t b0 = Wire.read();
    const uint8_t b1 = Wire.read();
    const uint8_t b2 = Wire.read();
    const uint32_t rv = (static_cast<uint32_t>(b0) << 16) |
                        (static_cast<uint32_t>(b1) << 8) |
                        (static_cast<uint32_t>(b2));
#endif

    /*
     * A zero result here means the chip had no conversion result to
     * give -- almost always because the convert command never landed.
     * Counted by the caller (pressure vs temperature) rather than
     * logged, see Counters.
     */
    return rv;
}

bool MS5611Sensor::readProm() {
    // Read all eight words (0xA0..0xAE) so the CRC nibble is available.
    for (int i = 0; i < 8; ++i) {
        const uint8_t address =
            static_cast<uint8_t>(kPromBase + (i * 2));

        Wire.beginTransmission(address_);
        Wire.write(address);

        const uint8_t error = Wire.endTransmission();
        if (error != 0) {
            DBGF("MS5611: PROM register 0x%02X select I2C error %u\n", address, error);
            return false;
        }

        const uint8_t received = Wire.requestFrom(address_, static_cast<uint8_t>(2));
        if (received != 2) {
            DBGF("MS5611: PROM register 0x%02X request got %u bytes, expected 2\n", address, received);
            return false;
        }

        const uint8_t msb = Wire.read();
        const uint8_t lsb = Wire.read();

        promWords_[i] =
            static_cast<uint16_t>(
                (static_cast<uint16_t>(msb) << 8) |
                static_cast<uint16_t>(lsb));
    }

    // A useful sanity check. Real MS5611 calibration coefficients
    // should not all be zero or obviously unprogrammed.
    bool allZero = true;

    for (int i = 1; i <= 6; ++i) {
        if (promWords_[i] != 0) {
            allZero = false;
            break;
        }
    }

    if (allZero) {
        DBGLN("MS5611: PROM coefficients all zero, treating as unprogrammed/absent device");
        return false;
    }

    /*
     * Verify the calibration data against its own CRC.
     *
     * We deliberately do NOT refuse to run on a CRC mismatch -- a
     * working barometer is more useful than none -- but a failure here
     * means every derived pressure and altitude is suspect, so it is
     * logged loudly and exposed via hasValidPromCrc().
     */
    const uint8_t expected = static_cast<uint8_t>(promWords_[7] & 0x000F);
    const uint8_t computed = crc4(promWords_);

    promCrcValid_ = (expected == computed);

    if (!promCrcValid_) {
        DBGF("MS5611: PROM CRC MISMATCH (expected %u, computed %u) -- "
             "calibration data is corrupt, pressure/altitude will be wrong\n",
             expected, computed);
    } else {
        DBGLN("MS5611: PROM CRC OK");
    }

    return true;
}

void MS5611Sensor::begin() {
    valid_ = false;
    state_ = State::WaitingPressure;

    pressure_ = 0.0f;
    temperature_ = 0.0f;
    altitude_ = 0.0f;
    relativeAltitude_ = 0.0f;

    pressureRaw_ = 0;
    temperatureRaw_ = 0;

    referencePressurePa_ = kSeaLevelPressurePa;
    referenceAltitude_ = 0.0f;
    referenceSet_ = false;
    promCrcValid_ = false;

    sampleSequence_ = 0;
    sampleTimeMs_ = 0;

    lastDebugMs_ = 0;

    // Find the MS5611.
    address_ = kAddressPrimary;

    Wire.beginTransmission(address_);
    bool found = (Wire.endTransmission() == 0);

    if (!found) {
        address_ = kAddressSecondary;

        Wire.beginTransmission(address_);
        found = (Wire.endTransmission() == 0);
    }

    if (!found) {
        DBGLN("MS5611: device not found at 0x76 or 0x77");
        return;
    }

    DBGF("MS5611: using I2C address 0x%02X\n", address_);

    // Reset.
    Wire.beginTransmission(address_);
    Wire.write(kResetCommand);

    const uint8_t resetError = Wire.endTransmission();
    if (resetError != 0) {
        DBGF("MS5611: reset command failed, I2C error %u\n", resetError);
        return;
    }

    // Datasheet specifies ~2.8ms reset time; use a safety margin.
    delay(10);

    // Read C1..C6.
    if (!readProm()) {
        DBGLN("MS5611: PROM read failed");
        return;
    }

    DBGF(
        "MS5611 PROM: C1=%u C2=%u C3=%u C4=%u C5=%u C6=%u (word0=0x%04X word7=0x%04X)\n",
        promWords_[1],
        promWords_[2],
        promWords_[3],
        promWords_[4],
        promWords_[5],
        promWords_[6],
        promWords_[0],
        promWords_[7]);

    valid_ = true;

    // Start the first pressure conversion immediately.
    requestConversion(kConvertD1);
    lastTransitionMs_ = millis();

    DBGLN("MS5611: initialized");
}

void MS5611Sensor::update() {
    const uint32_t now = millis();
    if (!valid_) {
        if (now - lastDebugMs_ >= Config::MS5611_DEBUG_INTERVAL_MS) {
            DBGLN("MS5611: sensor invalid");
            lastDebugMs_ = now;
        }
        return;
    }

    if (now - lastDebugMs_ >= Config::MS5611_DEBUG_INTERVAL_MS) {
        /*DBGF("MS5611: valid=%d rawP=%lu rawT=%lu pressure=%.2fPa temp=%.2fC alt=%.2fm rel=%.2fm\n",
                valid_ ? 1 : 0,
                static_cast<unsigned long>(pressureRaw_),
                static_cast<unsigned long>(temperatureRaw_),
                pressure_,
                temperature_ / 100.0f,
                altitude_,
                relativeAltitude_);
        lastDebugMs_ = now;
        }*/
    }

    switch (state_) {

        case State::WaitingPressure:

            if (now - lastTransitionMs_ < kConversionTimeMs) {
                return;
            }

        {
            // Keep the previous good value if this read fails, so the
            // diagnostics above still show the last real measurement.
            const uint32_t rawPressure = readAdc();

            if (rawPressure == 0) {
                ++counters_.adcFailPressure;

                // Try again.
                requestConversion(kConvertD1);
                lastTransitionMs_ = now;
                return;
            }

            pressureRaw_ = rawPressure;
        }
            //DBGLN("MS5611: pressure ADC read successful");

            // Immediately start temperature conversion.
            requestConversion(kConvertD2);

            lastTransitionMs_ = now;
            state_ = State::WaitingTemperature;

            break;


        case State::WaitingTemperature:

            if (now - lastTransitionMs_ < kConversionTimeMs) {
                return;
            }

        {
            const uint32_t rawTemperature = readAdc();

            if (rawTemperature == 0) {
                ++counters_.adcFailTemp;

                requestConversion(kConvertD1);
                lastTransitionMs_ = now;
                state_ = State::WaitingPressure;
                return;
            }

            temperatureRaw_ = rawTemperature;
        }
            //DBGLN("MS5611: temperature ADC read successful");

            processMeasurements();

            // Immediately start the next pressure conversion.
            requestConversion(kConvertD1);

            lastTransitionMs_ = now;
            state_ = State::WaitingPressure;

            break;
    }
}

void MS5611Sensor::processMeasurements() {
    /*
     * MS5611 first-order calculation.
     *
     * C1 = pressure sensitivity
     * C2 = pressure offset
     * C3 = temperature coefficient of pressure sensitivity
     * C4 = temperature coefficient of pressure offset
     * C5 = reference temperature
     * C6 = temperature coefficient of temperature
     */

    const uint32_t now = millis();

    // promWords_ is indexed by PROM address, so C1..C6 are [1]..[6].
    const int64_t dT =
        static_cast<int64_t>(temperatureRaw_) -
        (static_cast<int64_t>(promWords_[5]) << 8);

    // TEMP is in 0.01 degrees C.
    int64_t TEMP =
        2000 +
        ((dT * static_cast<int64_t>(promWords_[6])) >> 23);

    int64_t OFF =
        (static_cast<int64_t>(promWords_[2]) << 16) +
        ((static_cast<int64_t>(promWords_[4]) * dT) >> 7);

    int64_t SENS =
        (static_cast<int64_t>(promWords_[1]) << 15) +
        ((static_cast<int64_t>(promWords_[3]) * dT) >> 8);


    /*
     * Second-order temperature compensation.
     *
     * This is particularly important at lower temperatures and
     * improves the stability of the calculated pressure.
     *
     * IMPORTANT: these coefficients are chip-specific. This code
     * previously used the MS5607 values (61/2^4, 29/2^4, 17, 9), which
     * are wrong for the MS5611 and biased altitude below 20 C. The
     * values below are from the MS5611-01BA03 datasheet:
     *
     *   if (TEMP < 2000):
     *       T2    = dT^2 / 2^31
     *       OFF2  = 5 * (TEMP - 2000)^2 / 2
     *       SENS2 = 5 * (TEMP - 2000)^2 / 4
     *   if (TEMP < -1500):
     *       OFF2  += 7  * (TEMP + 1500)^2
     *       SENS2 += 11 * (TEMP + 1500)^2 / 2
     *
     * Do not copy these from an MS5607 driver -- the chips share a
     * register map and calculation shape but not these constants.
     */

    int64_t T2 = 0;
    int64_t OFF2 = 0;
    int64_t SENS2 = 0;

    if (TEMP < 2000) {
        T2 = (dT * dT) >> 31;

        const int64_t tempMinus2000 =
            TEMP - 2000;

        // 5 * t^2 / 2 and 5 * t^2 / 4, kept in integer arithmetic.
        OFF2 =
            (5LL * tempMinus2000 * tempMinus2000) >> 1;

        SENS2 =
            (5LL * tempMinus2000 * tempMinus2000) >> 2;

        if (TEMP < -1500) {

            const int64_t tempPlus1500 =
                TEMP + 1500;

            OFF2 +=
                7LL * tempPlus1500 * tempPlus1500;

            // 11 * t^2 / 2
            SENS2 +=
                (11LL * tempPlus1500 * tempPlus1500) >> 1;
        }
    }

    TEMP -= T2;
    OFF -= OFF2;
    SENS -= SENS2;


    // Pressure in 0.01 mbar / hPa.
    // Per datasheet: P = (D1*SENS/2^21 - OFF) / 2^15 -- the final >>15
    // applies to the whole expression, not just OFF.
    const int64_t P =
        (
            ((static_cast<int64_t>(pressureRaw_) * SENS) >> 21) - OFF
        ) >> 15;

    /*
     * P is in 0.01 mbar.
     * Convert to Pa:
     *
     * 1 mbar = 100 Pa
     * therefore 0.01 mbar = 1 Pa
     */

    const float newPressurePa =
        static_cast<float>(P);

    const float newTemperatureC =
        static_cast<float>(TEMP) / 100.0f;

    if (!isfinite(newPressurePa) ||
        !isfinite(newTemperatureC)) {
        DBGLN("MS5611: computed non-finite pressure/temperature, discarding sample");
        return;
    }

    // Reject obviously impossible pressure values.
    if (newPressurePa < 30000.0f ||
        newPressurePa > 120000.0f) {
        ++counters_.rangeReject;
        return;
    }

    /*
     * Validate into locals and only commit once every check has passed.
     *
     * The previous version assigned pressure_/temperature_/altitude_
     * first and range-checked afterwards, so a rejected sample had
     * already been published to getAltitude() by the time we bailed out.
     * That let a single bad reading reach the vario and the display.
     */
    const float newAltitude = pressureToAltitude(newPressurePa);

    if (!isfinite(newAltitude) ||
        newAltitude < -1000.0f ||
        newAltitude > 20000.0f) {
        ++counters_.rangeReject;
        return;
    }

    /*
     * Establish an initial reference when the first valid
     * measurement arrives.
     *
     * This gives a sensible relative altitude during pre-flight.
     * The reference is explicitly reset again when takeoff
     * is detected.
     */
    if (!referenceSet_) {
        referencePressurePa_ = newPressurePa;
        referenceAltitude_ = newAltitude;
        referenceSet_ = true;
    }

    // Sample accepted -- publish it as one consistent set.
    pressure_ = newPressurePa;
    temperature_ = newTemperatureC;
    altitude_ = newAltitude;

    // referenceAltitude_ is cached, so this costs no extra powf().
    relativeAltitude_ = newAltitude - referenceAltitude_;

    /*
     * Announce the new sample. Consumers (VarioCalculator via
     * Application) watch this counter so they only ingest genuinely
     * fresh readings, and use sampleTimeMs_ as the measurement time.
     */
    sampleTimeMs_ = now;
    ++sampleSequence_;
    ++counters_.samples;

    if (now - lastDebugMs_ >= Config::MS5611_DEBUG_INTERVAL_MS) {

        /*
         * Raw values are included so a bad reading can be traced to its
         * source without a rebuild: D1/D2 show what the chip returned,
         * dT/OFF/SENS show what the calibration did with it. A sane D1
         * with a wrong P means the coefficients are at fault; a wild D1
         * means the conversion or the bus is.
         */
        DBGF(
            "MS5611: P=%.2fPa T=%.2fC alt=%.2fm rel=%.2fm | D1=%lu D2=%lu "
            "dT=%ld OFF=%lld SENS=%lld crc=%s\n",
            pressure_,
            temperature_,
            altitude_,
            relativeAltitude_,
            static_cast<unsigned long>(pressureRaw_),
            static_cast<unsigned long>(temperatureRaw_),
            static_cast<long>(dT),
            static_cast<long long>(OFF),
            static_cast<long long>(SENS),
            promCrcValid_ ? "OK" : "BAD");

        lastDebugMs_ = now;
    }
}

float MS5611Sensor::pressureToAltitude(float pressurePa) const {
    if (pressurePa <= 0.0f) {
        return 0.0f;
    }

    return 44330.0f *
           (1.0f -
            powf(
                pressurePa / kSeaLevelPressurePa,
                kAltitudeExponent));
}

void MS5611Sensor::setTakeoffReference() {
    if (!valid_ || pressure_ <= 0.0f) {
        return;
    }

    referencePressurePa_ = pressure_;

    // Cache the matching altitude so relativeAltitude_ stays a single
    // subtraction rather than a second powf() on every sample.
    referenceAltitude_ = pressureToAltitude(pressure_);
    referenceSet_ = true;

    relativeAltitude_ = 0.0f;

    DBGF(
        "MS5611: takeoff reference set to %.2f Pa\n",
        referencePressurePa_);
}

bool MS5611Sensor::hasValidPromCrc() const {
    return promCrcValid_;
}

const MS5611Sensor::Counters& MS5611Sensor::getCounters() const {
    return counters_;
}

void MS5611Sensor::resetCounters() {
    counters_ = Counters{};
}

uint8_t MS5611Sensor::getLastI2cError() const {
    return lastI2cError_;
}

uint32_t MS5611Sensor::getRawPressure() const {
    return pressureRaw_;
}

uint32_t MS5611Sensor::getRawTemperature() const {
    return temperatureRaw_;
}

uint32_t MS5611Sensor::getSampleSequence() const {
    return sampleSequence_;
}

uint32_t MS5611Sensor::getSampleTimeMs() const {
    return sampleTimeMs_;
}

bool MS5611Sensor::isValid() const {
    return valid_;
}

float MS5611Sensor::getPressure() const {
    return pressure_;
}

float MS5611Sensor::getTemperature() const {
    return temperature_;
}

float MS5611Sensor::getAltitude() const {
    return altitude_;
}

float MS5611Sensor::getRelativeAltitude() const {
    return relativeAltitude_;
}

}  // namespace variometer