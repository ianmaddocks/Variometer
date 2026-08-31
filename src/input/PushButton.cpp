#include "input/PushButton.h"

#include "config/Config.h"

namespace variometer {

void PushButton::begin(uint8_t pin, const char* name) {
    pin_ = pin;
    name_ = name;
    pinMode(pin_, INPUT_PULLUP);
    lastRawState_ = (digitalRead(pin_) == LOW);
    stableState_ = lastRawState_;
    lastChangeMs_ = millis();
}

void PushButton::update() {
    wasPressed_ = false;

    const bool raw = (digitalRead(pin_) == LOW);
    const uint32_t now = millis();

    if (raw != lastRawState_) {
        lastRawState_ = raw;
        lastChangeMs_ = now;
    }

    if (raw != stableState_ && (now - lastChangeMs_) >= Config::BUTTON_DEBOUNCE_MS) {
        stableState_ = raw;

        if (stableState_) {
            wasPressed_ = true;
            DBGF("%s: pressed\n", name_);
        }
    }
}

bool PushButton::wasPressed() const {
    return wasPressed_;
}

}  // namespace variometer
