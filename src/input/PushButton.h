#pragma once

#include <Arduino.h>

namespace variometer {

/*
 * Simple debounced digital push button.
 *
 * Wired active-low (INPUT_PULLUP): pressed = LOW. Used for the discrete
 * SW1/SW2 buttons, which have no I2C chip of their own to debounce in
 * hardware, unlike the encoder's PUSHP/PUSHR edges.
 */
class PushButton {
public:
    void begin(uint8_t pin, const char* name);
    void update();

    // True for exactly one update() call per debounced press edge.
    bool wasPressed() const;

private:
    uint8_t pin_ = 0;
    const char* name_ = "";
    bool stableState_ = false;
    bool lastRawState_ = false;
    uint32_t lastChangeMs_ = 0;
    bool wasPressed_ = false;
};

}  // namespace variometer
