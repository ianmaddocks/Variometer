#include "core/Application.h"

#include <Arduino.h>
#include <math.h>
#include <Wire.h>

#include "config/Config.h"

namespace variometer {

Application::Application()
    : gps_(),
      ms5611_(),
      encoder_(),
      batteryMonitor_(),
      buzzer_(),
      flightDetector_(),
      varioCalculator_(),
      windEstimator_(),
      display_() {}

void Application::begin() {
    Serial.begin(115200);
    Wire.begin(Config::I2C_SDA, Config::I2C_SCL);

    gps_.begin();
    ms5611_.begin();
    encoder_.begin();
    batteryMonitor_.begin();
    powerManager_.begin();
    buzzer_.begin();
    display_.begin();
    display_.setPowerManager(&powerManager_);
    display_.setRecorder(&flightRecorder_);
    settings_.minSatellites = Config::MIN_SATELLITES_DEFAULT;

    buzzer_.playStartupTune();
}

void Application::loop() {
    const uint32_t now = millis();

    encoder_.update();

    if (encoder_.wasPressed()) {
        display_.handleButtonPress();
        if (flightData_.flightState == FlightState::PREFLIGHT) {
            flightDetector_.requestTakeoff();
        }
    }

    if (display_.isPowerOffScreen() && encoder_.consumeDoublePress()) {
        powerManager_.requestPowerOff();
        buzzer_.playPowerOffTune();
        Serial.println("Power-off sequence initiated by double button press on Power Off screen");
    }

    if (encoder_.wasDoublePressed()) {
        gps_.enableMockFeed();
    }

    if (encoder_.getDelta() != 0) {
        display_.handleEncoderDelta(encoder_.getDelta());
    }

    updateSensors();

    if (now - lastLogicMs_ >= 50) {
        updateFlightLogic();
        lastLogicMs_ = now;
    }

    if (now - lastAudioMs_ >= 20) {
        updateAudio();
        lastAudioMs_ = now;
    }

    if (now - lastDisplayMs_ >= Config::DISPLAY_UPDATE_INTERVAL_MS) {
        updateDisplay();
        lastDisplayMs_ = now;
    }

    if (powerManager_.shouldPowerOff()) {
        Serial.println("Power-off requested");
    }
}

void Application::updateSensors() {
    gps_.update();
    const uint32_t now = millis();

    if (now - lastMs5611Ms_ >= Config::MS5611_UPDATE_INTERVAL_MS) {
        ms5611_.update();
        lastMs5611Ms_ = now;
    }

    if (now - lastBatteryMs_ >= Config::BATTERY_UPDATE_INTERVAL_MS) {
        batteryMonitor_.update();
        lastBatteryMs_ = now;
    }

    if (gps_.hasData()) {
        flightData_.latitude = gps_.getLatitude();
        flightData_.longitude = gps_.getLongitude();
        flightData_.gpsAltitude = gps_.getAltitude();
        flightData_.groundSpeed = gps_.getGroundSpeed();
        flightData_.track = gps_.getTrack();
        flightData_.satellites = gps_.getSatellites();
        flightData_.gpsFix = gps_.getFixStatus();
    }
    if (flightData_.flightState == FlightState::TAKEOFF_DETECTED ||
        flightData_.flightState == FlightState::FLIGHT ||
        flightData_.flightState == FlightState::LANDING_DETECTED ||
        flightData_.flightState == FlightState::POST_FLIGHT) {
        if (flightStartTimeMs_ > 0) {
            flightData_.flightDuration = (millis() - flightStartTimeMs_) / 1000;
        }
    } else {
        flightData_.flightDuration = 0;
    }

    if (flightData_.hasLz && flightData_.gpsFix) {
        flightData_.distanceFromLZ = calculateDistanceFromLz(flightData_.latitude, flightData_.longitude);
    } else {
        flightData_.distanceFromLZ = 0.0f;
    }

    if (gps_.isMockEnabled() != lastMockMode) {
        lastMockMode = gps_.isMockEnabled();
        mockAltitudeInitialized = false;
        mockAltitudeBase = 0.0f;
        lastMockAltitude = 0.0f;
        lastMockAltitudeMs = 0;
        Serial.println("Mock Mode enabled");
    }

    if (gps_.isMockEnabled()) {
        if (!mockAltitudeInitialized) {
            mockAltitudeBase = gps_.getAltitude();
            lastMockAltitude = gps_.getAltitude();
            lastMockAltitudeMs = now;
            mockAltitudeInitialized = true;
            Serial.println("Mock Alt enabled");
        }

        const float currentAltitude = gps_.getAltitude();
        flightData_.barometricAltitude = currentAltitude;
        flightData_.relativeAltitude = currentAltitude - mockAltitudeBase;

        if ((now - lastMockAltitudeMs) >= 500) {  // Update derived vario every 500 ms
            const float dt = (now - lastMockAltitudeMs) / 1000.0f;  // Convert milliseconds to seconds
            if (dt > 0.0f) {
                float derivedVario = (currentAltitude - lastMockAltitude) / dt;
                if (derivedVario > 5.0f) {
                    derivedVario = 5.0f;
                } else if (derivedVario < -5.0f) {
                    derivedVario = -5.0f;
                }
                flightData_.verticalSpeed = derivedVario;
            }
            lastMockAltitude = currentAltitude;
            lastMockAltitudeMs = now;
     }

       
    } else if (ms5611_.isValid()) {
        flightData_.barometricAltitude = ms5611_.getAltitude();
        flightData_.relativeAltitude = ms5611_.getRelativeAltitude();
        flightData_.verticalSpeed = ms5611_.getVerticalSpeed();
    }

    if (flightData_.flightState == FlightState::FLIGHT || flightData_.flightState == FlightState::TAKEOFF_DETECTED) {
        flightRecorder_.addPoint(flightData_.barometricAltitude,
                                 static_cast<float>(millis()) / 1000.0f,
                                 flightData_.latitude,
                                 flightData_.longitude);
    }

    flightData_.tracePointCount = static_cast<uint16_t>(flightRecorder_.size());
    if (flightRecorder_.size() > 1) {
        const TracePoint& first = flightRecorder_.at(0);
        const TracePoint& latest = flightRecorder_.at(flightRecorder_.size() - 1);
        flightData_.traceAltitudeMin = first.altitude;
        flightData_.traceAltitudeMax = latest.altitude;
        flightData_.traceAltitudeSpan = latest.altitude - first.altitude;
    } else {
        flightData_.traceAltitudeMin = flightData_.barometricAltitude;
        flightData_.traceAltitudeMax = flightData_.barometricAltitude;
        flightData_.traceAltitudeSpan = 0.0f;
    }

    flightData_.batteryVoltage = batteryMonitor_.getVoltage();
    flightData_.batteryPercent = batteryMonitor_.getPercent();

}

void Application::updateFlightLogic() {
    const FlightState previousState = flightData_.flightState;
    flightDetector_.update(flightData_);
    const FlightState detectorState = flightDetector_.getState();
    if (previousState == FlightState::PREFLIGHT && detectorState != FlightState::PREFLIGHT) {
        initializeFlightSession();
        buzzer_.playTakeoffTone();
    }
    flightData_.flightState = detectorState;
    varioCalculator_.update(flightData_);
    flightData_.verticalSpeed = varioCalculator_.getVerticalSpeed();
    windEstimator_.update(flightData_);
    flightData_.windSpeed = windEstimator_.getWindSpeed();
    flightData_.windDirection = windEstimator_.getWindDirection();
    flightData_.windConfidence = windEstimator_.getWindConfidence();
}

void Application::updateAudio() {
    buzzer_.update();
    buzzer_.updateVarioFeedback(flightData_.verticalSpeed);
}

void Application::initializeFlightSession() {
    if (flightData_.hasLz) {
        return;
    }

    if (flightData_.gpsFix) {
        flightData_.lzLatitude = flightData_.latitude;
        flightData_.lzLongitude = flightData_.longitude;
        flightData_.hasLz = true;
    }

    flightRecorder_.clear();
    flightStartTimeMs_ = millis();
    flightData_.flightDuration = 0;
    flightData_.distanceFromLZ = 0.0f;
    flightData_.tracePointCount = 0;

    Serial.printf("Flight session initialized at %.5f, %.5f\n",
                  flightData_.lzLatitude,
                  flightData_.lzLongitude);
}

float Application::calculateDistanceFromLz(float latitude, float longitude) const {
    if (!flightData_.hasLz) {
        return 0.0f;
    }

    const float lat1Rad = latitude * 0.017453292519943295f;
    const float lon1Rad = longitude * 0.017453292519943295f;
    const float lat2Rad = flightData_.lzLatitude * 0.017453292519943295f;
    const float lon2Rad = flightData_.lzLongitude * 0.017453292519943295f;

    const float dLat = lat2Rad - lat1Rad;
    const float dLon = lon2Rad - lon1Rad;
    const float a = sinf(dLat * 0.5f) * sinf(dLat * 0.5f) +
                   cosf(lat1Rad) * cosf(lat2Rad) * sinf(dLon * 0.5f) * sinf(dLon * 0.5f);
    const float c = 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));
    return 6371.0f * c;
}

void Application::updateDisplay() {
    display_.update(flightData_);
}

}  // namespace variometer
