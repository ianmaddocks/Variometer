#pragma once

#include <Arduino.h>
#include <i2cEncoderLibV2.h>

namespace variometer {

class Encoder {
public:
    void begin();
    void update();

    int8_t getDelta() const;
    bool wasPressed() const;
    bool wasDoublePressed() const;
    bool consumeDoublePress();

    // True for exactly one update() call once the button has been held
    // down continuously for Config::POWER_OFF_HOLD_MS.
    bool consumeLongPress();

private:
    i2cEncoderLibV2* encoder_;
    int8_t delta_ = 0;
    bool wasPressed_ = false;
    bool wasDoublePressed_ = false;

    // Software hold-duration tracking, derived from the chip's press
    // (PUSHP) / release (PUSHR) edge events.
    bool held_ = false;
    uint32_t pressStartMs_ = 0;
    bool longPressFired_ = false;
    bool longPressPending_ = false;
};

}  // namespace variometer
