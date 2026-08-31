#pragma once

#include <Arduino.h>

namespace variometer {

/*
 * Non-blocking haptic motor feedback (NPN transistor on Config::HAPTIC_PIN).
 *
 * Replaces the buzzer for vario feedback now that the buzzer and haptic
 * motor share the same physical pin (see Config::HAPTIC_PIN) -- only one
 * of the two can be wired at a time. Ascent and descent are distinguished
 * by pulse length rather than tone, since a vibration motor has no pitch.
 */
class Haptic {
public:
    Haptic() = default;
    void begin();
    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled_; }
    void update();
    void updateVarioFeedback(float verticalSpeed);

private:
    enum class PulseState : uint8_t {
        Idle,
        On
    };

    PulseState pulseState_ = PulseState::Idle;
    uint32_t pulseOffMs_ = 0;
    uint32_t lastVarioFeedbackMs_ = 0;
    bool enabled_ = false;
};

}  // namespace variometer
