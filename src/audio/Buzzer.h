#pragma once

#include <Arduino.h>

namespace variometer {

class Buzzer {
public:
    Buzzer() = default;
    void begin();
    void update();
    void playStartupTune();
    void playTakeoffTone();
    void playPowerOffTune();
    void updateVarioFeedback(float verticalSpeed);

private:
    enum class PulseState : uint8_t {
        Idle,
        High,
        Low,
        High2,
        Low2
    };

    PulseState pulseState_ = PulseState::Idle;
    uint32_t nextToggleMs_ = 0;
    uint32_t toneLengthMs_ = 80;
    uint32_t toneGapMs_ = 80;
    bool enabled_ = false;
    uint32_t lastVarioFeedbackMs_ = 0;
    float lastVarioSpeed_ = 0.0f;
};

}  // namespace variometer
