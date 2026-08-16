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

void MS5611Sensor::requestConversion(uint8_t command) {
    Wire.beginTransmission(address_);
    Wire.write(command);
    Wire.endTransmission();
}

uint32_t MS5611Sensor::readAdc() {
    Wire.beginTransmission(address_);
    Wire.write(kReadAdc);

    const uint8_t error = Wire.endTransmission();

    if (error != 0) {
        DBGF("MS5611: ADC register select I2C error %u\n", error);
        return 0;
    }

    const uint8_t requested = 3;
    const uint8_t received = Wire.requestFrom(address_, requested);

    if (received != requested) {
        DBGF( "MS5611: ADC request got %u bytes, expected %u\n", received, requested);
        return 0;
    }

    const uint8_t b0 = Wire.read();
    const uint8_t b1 = Wire.read();
    const uint8_t b2 = Wire.read();
    //DBG( "Read ADC successfully\n");

    uint32_t rv = (static_cast<uint32_t>(b0) << 16) |
                  (static_cast<uint32_t>(b1) << 8) |
                  (static_cast<uint32_t>(b2));
    if (rv == 0) {
        DBGLN("MS5611: ADC read returned zero");
        DBGF("MS5611: ADC bytes: %02X %02X %02X\n", b0, b1, b2);
        //rv = 1; //the smallest non-zero value to avoid divide-by-zero errors in calculations.
    }
    return rv;
}

bool MS5611Sensor::readProm() {
    for (int i = 0; i < 6; ++i) {
        const uint8_t address =
            static_cast<uint8_t>(kPromBase + (i * 2));

        Wire.beginTransmission(address_);
        Wire.write(address);

        if (Wire.endTransmission() != 0) {
            return false;
        }

        if (Wire.requestFrom(address_, static_cast<uint8_t>(2)) != 2) {
            return false;
        }

        const uint8_t msb = Wire.read();
        const uint8_t lsb = Wire.read();

        prom_[i] =
            static_cast<uint16_t>(
                (static_cast<uint16_t>(msb) << 8) |
                static_cast<uint16_t>(lsb));
    }

    // A useful sanity check. Real MS5611 calibration coefficients
    // should not all be zero or obviously unprogrammed.
    bool allZero = true;

    for (int i = 0; i < 6; ++i) {
        if (prom_[i] != 0) {
            allZero = false;
            break;
        }
    }

    return !allZero;
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
    referenceSet_ = false;

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

    if (Wire.endTransmission() != 0) {
        DBGLN("MS5611: reset command failed");
        return;
    }

    delay(3);

    // Read C1..C6.
    if (!readProm()) {
        DBGLN("MS5611: PROM read failed");
        return;
    }

    DBGF(
        "MS5611 PROM: C1=%u C2=%u C3=%u C4=%u C5=%u C6=%u\n",
        prom_[0],
        prom_[1],
        prom_[2],
        prom_[3],
        prom_[4],
        prom_[5]);

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

            pressureRaw_ = readAdc();

            if (pressureRaw_ == 0) {
                DBGLN("MS5611: invalid pressure ADC read");

                // Try again.
                requestConversion(kConvertD1); 
                lastTransitionMs_ = now;
                return;
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

            temperatureRaw_ = readAdc();

            if (temperatureRaw_ == 0) {
                DBGLN("MS5611: invalid temperature ADC read");

                requestConversion(kConvertD1);
                lastTransitionMs_ = now;
                state_ = State::WaitingPressure;
                return;
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

    const int64_t dT =
        static_cast<int64_t>(temperatureRaw_) -
        (static_cast<int64_t>(prom_[4]) << 8);

    // TEMP is in 0.01 degrees C.
    int64_t TEMP =
        2000 +
        ((dT * static_cast<int64_t>(prom_[5])) >> 23);

    int64_t OFF =
        (static_cast<int64_t>(prom_[1]) << 16) +
        ((static_cast<int64_t>(prom_[3]) * dT) >> 7);

    int64_t SENS =
        (static_cast<int64_t>(prom_[0]) << 15) +
        ((static_cast<int64_t>(prom_[2]) * dT) >> 8);


    /*
     * Second-order temperature compensation.
     *
     * This is particularly important at lower temperatures and
     * improves the stability of the calculated pressure.
     */

    int64_t T2 = 0;
    int64_t OFF2 = 0;
    int64_t SENS2 = 0;

    if (TEMP < 2000) {
        T2 =
            (3LL * dT * dT) >> 33;

        const int64_t tempMinus2000 =
            TEMP - 2000;

        OFF2 =
            (61LL * tempMinus2000 * tempMinus2000) >> 4;

        SENS2 =
            (29LL * tempMinus2000 * tempMinus2000) >> 4;

        if (TEMP < -1500) {

            const int64_t tempPlus1500 =
                TEMP + 1500;

            OFF2 +=
                17LL * tempPlus1500 * tempPlus1500;

            SENS2 +=
                9LL * tempPlus1500 * tempPlus1500;
        }
    }

    TEMP -= T2;
    OFF -= OFF2;
    SENS -= SENS2;


    // Pressure in 0.01 mbar / hPa.
    const int64_t P =
        (
            (static_cast<int64_t>(pressureRaw_) * SENS)
            >> 21
        ) - (OFF >> 15);

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
        DBGF("MS5611: pressure %.2fPa out of range, discarding sample\n", newPressurePa);
        return;
    }

    pressure_ = newPressurePa;
    temperature_ = newTemperatureC;

    altitude_ = pressureToAltitude(pressure_);

    if (!isfinite(altitude_) ||
        altitude_ < -1000.0f ||
        altitude_ > 20000.0f) {
        DBGF("MS5611: altitude %.2fm out of range, discarding sample\n", altitude_);
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
        referencePressurePa_ = pressure_;
        referenceSet_ = true;
    }

    relativeAltitude_ =
        pressureToAltitude(pressure_) -
        pressureToAltitude(referencePressurePa_);

    if (now - lastDebugMs_ >= Config::MS5611_DEBUG_INTERVAL_MS) {

        DBGF(
            "MS5611: P=%.2fPa T=%.2fC alt=%.2fm rel=%.2fm\n",
            pressure_,
            temperature_,
            altitude_,
            relativeAltitude_);

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
    referenceSet_ = true;

    relativeAltitude_ = 0.0f;

    DBGF(
        "MS5611: takeoff reference set to %.2f Pa\n",
        referencePressurePa_);
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