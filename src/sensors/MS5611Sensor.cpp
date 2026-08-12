#include "sensors/MS5611Sensor.h"

#include <math.h>
#include <Wire.h>

namespace variometer {
namespace {
constexpr uint8_t kAddress = 0x77;
constexpr uint8_t kResetCommand = 0x1E;
constexpr uint8_t kPromBase = 0xA0;
constexpr uint8_t kReadAdc = 0x00;
constexpr uint8_t kConvertD1 = 0x40;
constexpr uint8_t kConvertD2 = 0x50;
}  // namespace

void MS5611Sensor::requestConversion(uint8_t command) {
    Wire.beginTransmission(kAddress);
    Wire.write(command);
    Wire.endTransmission();
}

uint32_t MS5611Sensor::readAdc() {
    Wire.beginTransmission(kAddress);
    Wire.write(kReadAdc);
    if (Wire.endTransmission() != 0) {
        return 0;
    }

    Wire.requestFrom(kAddress, static_cast<uint8_t>(3));
    uint32_t value = 0;
    for (int i = 0; i < 3 && Wire.available(); ++i) {
        value = (value << 8) | Wire.read();
    }
    return value;
}

bool MS5611Sensor::readProm() {
    for (int i = 0; i < 6; ++i) {
        Wire.beginTransmission(kAddress);
        Wire.write(static_cast<uint8_t>(kPromBase + (i * 2)));
        if (Wire.endTransmission() != 0) {
            return false;
        }
        Wire.requestFrom(kAddress, static_cast<uint8_t>(2));
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

    Wire.beginTransmission(kAddress);
    if (Wire.endTransmission() != 0) {
        return;
    }

    Wire.beginTransmission(kAddress);
    Wire.write(kResetCommand);
    if (Wire.endTransmission() != 0) {
        return;
    }
    delay(3);

    if (!readProm()) {
        return;
    }

    valid_ = true;
    previousAltitude_ = 0.0f;
    previousAltitudeMs_ = millis();
    baseAltitude_ = 0.0f;
}

void MS5611Sensor::update() {
    if (!valid_) {
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
                const float temperature = 2000.0f + (dT / 8388608.0f);
                const int64_t off = (static_cast<int64_t>(prom_[1]) << 16) + ((static_cast<int64_t>(prom_[3]) * dT) >> 7);
                const int64_t sens = (static_cast<int64_t>(prom_[0]) << 15) + ((static_cast<int64_t>(prom_[2]) * dT) >> 8);
                const float pressure = (static_cast<float>(pressureRaw_) * static_cast<float>(sens) / 2097152.0f) - static_cast<float>(off) / 32768.0f;
                const float computedAltitude = 44330.0f * (1.0f - powf(pressure / 101325.0f, 1.0f / 5.255f));

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
                state_ = State::Idle;
            }
            break;
    }
}

bool MS5611Sensor::isValid() const { return valid_; }
float MS5611Sensor::getAltitude() const { return altitude_; }
float MS5611Sensor::getRelativeAltitude() const { return relativeAltitude_; }
float MS5611Sensor::getVerticalSpeed() const { return verticalSpeed_; }

}  // namespace variometer
