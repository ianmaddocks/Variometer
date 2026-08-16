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
        // Warm-up phase: rollingAverage_ accumulates a running sum of the
        // first BATTERY_SAMPLES readings; voltage_ is the true average so
        // far, computed fresh each time.
        rollingAverage_ += measuredVoltage;
        ++sampleCount_;
        voltage_ = rollingAverage_ / static_cast<float>(sampleCount_);

        if (sampleCount_ == Config::BATTERY_SAMPLES) {
            // Convert rollingAverage_ from "sum of N samples" to a true
            // voltage-scale average so the exponential filter below blends
            // like-for-like quantities instead of mixing a sum with a
            // per-sample reading.
            rollingAverage_ = voltage_;
        }
    } else {
        // Steady state: rollingAverage_ is already voltage-scaled here.
        rollingAverage_ = rollingAverage_ * 0.8f + measuredVoltage * 0.2f;
        voltage_ = rollingAverage_;
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
