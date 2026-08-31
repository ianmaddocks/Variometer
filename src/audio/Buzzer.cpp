#include "audio/Buzzer.h"

#include <Arduino.h>

#include "config/Config.h"

/*
 * D0 now drives a haptic motor instead of the buzzer (see Haptic.h /
 * Config::HAPTIC_PIN) -- the two shared the same physical pin and the
 * board only has the one wired up. The real implementation is kept
 * below, disabled, rather than deleted, in case a buzzer is wired back
 * in on a free pin later. Every method below is a deliberate no-op.
 */
#define BUZZER_HARDWARE_PRESENT 0

namespace variometer {

void Buzzer::begin() {
#if BUZZER_HARDWARE_PRESENT
    pinMode(Config::BUZZER_PIN, OUTPUT);
    digitalWrite(Config::BUZZER_PIN, LOW);
    enabled_ = false;
    pulseState_ = PulseState::Idle;
#endif
}

void Buzzer::setEnabled(bool enabled) {
#if BUZZER_HARDWARE_PRESENT
    enabled_ = enabled;
    if (!enabled_) {
        pulseState_ = PulseState::Idle;
        digitalWrite(Config::BUZZER_PIN, LOW);
    }
#else
    (void)enabled;
#endif
}

void Buzzer::update() {
#if BUZZER_HARDWARE_PRESENT
    if (!enabled_ || pulseState_ == PulseState::Idle) {
        return;
    }

    const uint32_t now = millis();
    if (now < nextToggleMs_) {
        return;
    }

    switch (pulseState_) {
        case PulseState::High:
            digitalWrite(Config::BUZZER_PIN, HIGH);
            nextToggleMs_ = now + toneLengthMs_;
            pulseState_ = PulseState::Low;
            break;
        case PulseState::Low:
            digitalWrite(Config::BUZZER_PIN, LOW);
            nextToggleMs_ = now + toneGapMs_;
            pulseState_ = PulseState::High2;
            break;
        case PulseState::High2:
            digitalWrite(Config::BUZZER_PIN, HIGH);
            nextToggleMs_ = now + toneLengthMs_;
            pulseState_ = PulseState::Low2;
            break;
        case PulseState::Low2:
            digitalWrite(Config::BUZZER_PIN, LOW);
            pulseState_ = PulseState::Idle;
            break;
        default:
            pulseState_ = PulseState::Idle;
            break;
    }
#endif
}

void Buzzer::playStartupTune() {
#if BUZZER_HARDWARE_PRESENT
    if (!enabled_) {
        return;
    }
    toneLengthMs_ = 70;
    toneGapMs_ = 100;
    pulseState_ = PulseState::High;
    nextToggleMs_ = millis();
    digitalWrite(Config::BUZZER_PIN, LOW);
#endif
}

void Buzzer::playTakeoffTone() {
#if BUZZER_HARDWARE_PRESENT
    if (!enabled_) {
        return;
    }
    toneLengthMs_ = 120;
    toneGapMs_ = 60;
    pulseState_ = PulseState::High;
    nextToggleMs_ = millis();
    digitalWrite(Config::BUZZER_PIN, LOW);
#endif
}

void Buzzer::playPowerOffTune() {
#if BUZZER_HARDWARE_PRESENT
    if (!enabled_) {
        return;
    }
    toneLengthMs_ = 150;
    toneGapMs_ = 140;
    pulseState_ = PulseState::High;
    nextToggleMs_ = millis();
    digitalWrite(Config::BUZZER_PIN, LOW);
#endif
}

void Buzzer::updateVarioFeedback(float verticalSpeed) {
#if BUZZER_HARDWARE_PRESENT
    if (!enabled_) {
        return;
    }

    const uint32_t now = millis();
    if (now - lastVarioFeedbackMs_ < 120) {
        return;
    }

    if (verticalSpeed > 0.6f) {
        toneLengthMs_ = 50;
        toneGapMs_ = 90;
        pulseState_ = PulseState::High;
        nextToggleMs_ = now;
        lastVarioFeedbackMs_ = now;
    } else if (verticalSpeed < -0.6f) {
        toneLengthMs_ = 90;
        toneGapMs_ = 50;
        pulseState_ = PulseState::High;
        nextToggleMs_ = now;
        lastVarioFeedbackMs_ = now;
    } else {
        lastVarioSpeed_ = verticalSpeed;
    }
#endif
}

}  // namespace variometer
