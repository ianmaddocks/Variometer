#include "power/BatteryMonitor.h"

#include <Arduino.h>

#include "config/Config.h"

namespace variometer {

void BatteryMonitor::begin() {
    pinMode(Config::BATTERY_PIN, INPUT);
    analogReadResolution(12);
}

void BatteryMonitor::update() {
    const int raw = analogRead(Config::BATTERY_PIN);
    const float adcVoltage = raw * (3.3f / 4095.0f);
    const float measuredVoltage = adcVoltage * Config::BATTERY_DIVIDER_RATIO;

    if (sampleCount_ < Config::BATTERY_SAMPLES) {
        rollingAverage_ += measuredVoltage;
        ++sampleCount_;
    } else {
        rollingAverage_ = rollingAverage_ * 0.8f + measuredVoltage * 0.2f;
    }

    if (sampleCount_ >= 1) {
        voltage_ = rollingAverage_ / (sampleCount_ < Config::BATTERY_SAMPLES ? sampleCount_ : 1.0f);
    } else {
        voltage_ = measuredVoltage;
    }

    if (voltage_ <= Config::BATTERY_MIN_VOLTAGE) {
        percent_ = 0.0f;
    } else if (voltage_ >= Config::BATTERY_MAX_VOLTAGE) {
        percent_ = 100.0f;
    } else {
        const float span = Config::BATTERY_MAX_VOLTAGE - Config::BATTERY_MIN_VOLTAGE;
        percent_ = ((voltage_ - Config::BATTERY_MIN_VOLTAGE) / span) * 100.0f;
    }

    if (percent_ < 0.0f) {
        percent_ = 0.0f;
    } else if (percent_ > 100.0f) {
        percent_ = 100.0f;
    }
}

float BatteryMonitor::getVoltage() const { return voltage_; }
float BatteryMonitor::getPercent() const { return percent_; }
bool BatteryMonitor::isLow() const { return voltage_ <= Config::BATTERY_WARN_VOLTAGE; }
bool BatteryMonitor::isCritical() const { return voltage_ <= Config::BATTERY_CRITICAL_VOLTAGE; }

}  // namespace variometer
