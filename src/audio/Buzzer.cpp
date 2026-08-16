#include "audio/Buzzer.h"

#include <Arduino.h>

#include "config/Config.h"

namespace variometer {

void Buzzer::begin() {
    pinMode(Config::BUZZER_PIN, OUTPUT);
    digitalWrite(Config::BUZZER_PIN, LOW);
    enabled_ = false;
    pulseState_ = PulseState::Idle;
}

void Buzzer::setEnabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled_) {
        pulseState_ = PulseState::Idle;
        digitalWrite(Config::BUZZER_PIN, LOW);
    }
}

void Buzzer::update() {
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
}

void Buzzer::playStartupTune() {
    if (!enabled_) {
        return;
    }
    toneLengthMs_ = 70;
    toneGapMs_ = 100;
    pulseState_ = PulseState::High;
    nextToggleMs_ = millis();
    digitalWrite(Config::BUZZER_PIN, LOW);
}

void Buzzer::playTakeoffTone() {
    if (!enabled_) {
        return;
    }
    toneLengthMs_ = 120;
    toneGapMs_ = 60;
    pulseState_ = PulseState::High;
    nextToggleMs_ = millis();
    digitalWrite(Config::BUZZER_PIN, LOW);
}

void Buzzer::playPowerOffTune() {
    if (!enabled_) {
        return;
    }
    toneLengthMs_ = 150;
    toneGapMs_ = 140;
    pulseState_ = PulseState::High;
    nextToggleMs_ = millis();
    digitalWrite(Config::BUZZER_PIN, LOW);
}

void Buzzer::updateVarioFeedback(float verticalSpeed) {
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
}

}  // namespace variometer
