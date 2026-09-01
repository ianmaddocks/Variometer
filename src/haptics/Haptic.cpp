#include "haptics/Haptic.h"

#include "config/Config.h"

namespace variometer {
namespace {
// Retrigger cooldown, matching the buzzer's vario feedback cadence.
constexpr uint32_t kFeedbackCooldownMs = 120;

// Ascent: short, quick taps. Descent: one long, sustained buzz. Chosen
// to be distinguishable by feel alone, since a vibration motor -- unlike
// the buzzer it replaces -- cannot signal direction with pitch.
constexpr uint32_t kAscentPulseMs = 40;
constexpr uint32_t kDescentPulseMs = 200;
constexpr float kVarioFeedbackThreshold = 0.6f;
}  // namespace

void Haptic::begin() {
    pinMode(Config::HAPTIC_PIN, OUTPUT);
    digitalWrite(Config::HAPTIC_PIN, LOW);
    enabled_ = true;
}

void Haptic::setEnabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled_) {
        pulseState_ = PulseState::Idle;
        digitalWrite(Config::HAPTIC_PIN, LOW);
    }
}

void Haptic::update() {
    if (!enabled_ || pulseState_ != PulseState::On) {
        return;
    }

    if (millis() < pulseOffMs_) {
        return;
    }

    digitalWrite(Config::HAPTIC_PIN, LOW);
    pulseState_ = PulseState::Idle;
}

void Haptic::triggerPulse(uint32_t durationMs) {
    if (!enabled_) {
        return;
    }

    digitalWrite(Config::HAPTIC_PIN, HIGH);
    pulseOffMs_ = millis() + durationMs;
    pulseState_ = PulseState::On;
}

void Haptic::updateVarioFeedback(float verticalSpeed) {
    if (!enabled_) {
        return;
    }

    const uint32_t now = millis();
    if (now - lastVarioFeedbackMs_ < kFeedbackCooldownMs) {
        return;
    }

    uint32_t pulseMs = 0;
    if (verticalSpeed > kVarioFeedbackThreshold) {
        pulseMs = kAscentPulseMs;
    } else if (verticalSpeed < -kVarioFeedbackThreshold) {
        pulseMs = kDescentPulseMs;
    } else {
        return;
    }

    triggerPulse(pulseMs);
    lastVarioFeedbackMs_ = now;
}

}  // namespace variometer
