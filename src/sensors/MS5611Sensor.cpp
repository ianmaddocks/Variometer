#include "sensors/MS5611Sensor.h"

#include <math.h>
#include <Wire.h>

#include "config/Config.h"

namespace variometer {
namespace {
constexpr uint8_t kAddressPrimary = 0x76;
constexpr uint8_t kAddressSecondary = 0x77;
constexpr uint8_t kResetCommand = 0x1E;
constexpr uint8_t kPromBase = 0xA0;
constexpr uint8_t kReadAdc = 0x00;
constexpr uint8_t kConvertD1 = 0x40;
constexpr uint8_t kConvertD2 = 0x50;
}  // namespace

void MS5611Sensor::requestConversion(uint8_t command) {
    Wire.beginTransmission(address_);
    Wire.write(command);
    Wire.endTransmission();
}

uint32_t MS5611Sensor::readAdc() {
    Wire.beginTransmission(address_);
    Wire.write(kReadAdc);
    if (Wire.endTransmission() != 0) {
        return 0;
    }

    Wire.requestFrom(address_, static_cast<uint8_t>(3));
    uint32_t value = 0;
    for (int i = 0; i < 3 && Wire.available(); ++i) {
        value = (value << 8) | Wire.read();
    }
    return value;
}

bool MS5611Sensor::readProm() {
    for (int i = 0; i < 6; ++i) {
        Wire.beginTransmission(address_);
        Wire.write(static_cast<uint8_t>(kPromBase + (i * 2)));
        if (Wire.endTransmission() != 0) {
            return false;
        }
        Wire.requestFrom(address_, static_cast<uint8_t>(2));
        if (Wire.available() < 2) {
            return false;
        }
        prom_[i] = static_cast<uint16_t>((Wire.read() << 8) | Wire.read());
    }
    return true;
}

void MS5611Sensor::begin() {
    valid_ = false;
    state_ = State::Idle;
    lastTransitionMs_ = millis();
    lastDebugMs_ = 0;

    address_ = kAddressPrimary;
    Wire.beginTransmission(address_);
    bool found = Wire.endTransmission() == 0;
    if (!found) {
        address_ = kAddressSecondary;
        Wire.beginTransmission(address_);
        found = Wire.endTransmission() == 0;
    }
    if (!found) {
        DBGLN("MS5611: device not found at 0x76 or 0x77");
        return;
    }

    DBGF("MS5611: using I2C address 0x%02X\n", address_);

    Wire.beginTransmission(address_);
    Wire.write(kResetCommand);
    if (Wire.endTransmission() != 0) {
        DBGLN("MS5611: reset command failed");
        return;
    }
    delay(3);

    if (!readProm()) {
        DBGLN("MS5611: PROM read failed");
        return;
    }

    valid_ = true;
    previousAltitude_ = 0.0f;
    previousAltitudeMs_ = millis();
    baseAltitude_ = 0.0f;
    DBGLN("MS5611: initialized");
}

void MS5611Sensor::update() {
    if (!valid_) {
        const uint32_t now = millis();
        if (now - lastDebugMs_ >= Config::MS5611_DEBUG_INTERVAL_MS) {
            DBGLN("MS5611: no data; sensor is invalid at both 0x76 and 0x77");
            lastDebugMs_ = now;
        }
        return;
    }

    const uint32_t now = millis();
    switch (state_) {
        case State::Idle:
            if (now - lastTransitionMs_ >= 20) {
                requestConversion(kConvertD1);
                lastTransitionMs_ = now;
                state_ = State::WaitingPressure;
            }
            break;
        case State::WaitingPressure:
            if (now - lastTransitionMs_ >= 10) {
                pressureRaw_ = readAdc();
                requestConversion(kConvertD2);
                lastTransitionMs_ = now;
                state_ = State::WaitingTemperature;
            }
            break;
        case State::WaitingTemperature:
            if (now - lastTransitionMs_ >= 10) {
                temperatureRaw_ = readAdc();

                const int32_t dT = static_cast<int32_t>(temperatureRaw_) - (static_cast<int32_t>(prom_[4]) << 8);
                temperature_ = 2000.0f + (dT / 8388608.0f);
                const int64_t off = (static_cast<int64_t>(prom_[1]) << 16) + ((static_cast<int64_t>(prom_[3]) * dT) >> 7);
                const int64_t sens = (static_cast<int64_t>(prom_[0]) << 15) + ((static_cast<int64_t>(prom_[2]) * dT) >> 8);
                pressure_ = (static_cast<float>(pressureRaw_) * static_cast<float>(sens) / 2097152.0f) - static_cast<float>(off) / 32768.0f;
                const float computedAltitude = 44330.0f * (1.0f - powf(pressure_ / 101325.0f, 1.0f / 5.255f));

                if (isfinite(computedAltitude) && computedAltitude > -1000.0f && computedAltitude < 20000.0f) {
                    altitude_ = computedAltitude;
                }

                if (baseAltitude_ == 0.0f) {
                    baseAltitude_ = altitude_;
                }
                relativeAltitude_ = altitude_ - baseAltitude_;

                const uint32_t elapsedMs = now - previousAltitudeMs_;
                if (elapsedMs > 0) {
                    const float elapsedSeconds = elapsedMs / 1000.0f;
                    const float computedVario = (altitude_ - previousAltitude_) / elapsedSeconds;
                    if (isfinite(computedVario) && fabsf(computedVario) < 50.0f) {
                        verticalSpeed_ = computedVario;
                    }
                }
                previousAltitude_ = altitude_;
                previousAltitudeMs_ = now;
                if (now - lastDebugMs_ >= Config::MS5611_DEBUG_INTERVAL_MS) {
                    DBGF("MS5611: valid=%d rawP=%lu rawT=%lu pressure=%.2fPa temp=%.2fC alt=%.2fm rel=%.2fm vario=%.2fm/s\n",
                         valid_ ? 1 : 0,
                         static_cast<unsigned long>(pressureRaw_),
                         static_cast<unsigned long>(temperatureRaw_),
                         pressure_,
                         temperature_ / 100.0f,
                         altitude_,
                         relativeAltitude_,
                         verticalSpeed_);
                    lastDebugMs_ = now;
                }
                state_ = State::Idle;
            }
            break;
    }
}

bool MS5611Sensor::isValid() const { return valid_; }
float MS5611Sensor::getPressure() const { return pressure_; }
float MS5611Sensor::getTemperature() const { return temperature_ / 100.0f; }
float MS5611Sensor::getAltitude() const { return altitude_; }
float MS5611Sensor::getRelativeAltitude() const { return relativeAltitude_; }
float MS5611Sensor::getVerticalSpeed() const { return verticalSpeed_; }

}  // namespace variometer
