#pragma once

#include <Arduino.h>

namespace variometer {

class BatteryMonitor {
public:
    BatteryMonitor() = default;
    void begin();
    void update();
    float getVoltage() const;
    float getPercent() const;
    bool isLow() const;
    bool isCritical() const;

private:
    float voltage_ = 0.0f;
    float percent_ = 0.0f;
    uint32_t sampleCount_ = 0;
    float rollingAverage_ = 0.0f;
};

}  // namespace variometer
