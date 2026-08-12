#include "input/Encoder.h"
#include <Wire.h>
#include "config/Config.h"


namespace variometer {

void Encoder::begin() {
    Wire.begin();
    encoder_ = new i2cEncoderLibV2(Config::ENCODER_I2C_ADDRESS);
    encoder_->begin(i2cEncoderLibV2::INT_DATA   | i2cEncoderLibV2::WRAP_DISABLE |
                    i2cEncoderLibV2::DIRE_RIGHT | i2cEncoderLibV2::IPUP_ENABLE |
                        i2cEncoderLibV2::RMOD_X1    | i2cEncoderLibV2::RGB_ENCODER);

    encoder_->writeCounter((int32_t) 0); /* Reset the counter value */
    encoder_->writeMax((int32_t) 1); /* Set the maximum threshold*/
    encoder_->writeMin((int32_t) 0); /* Set the minimum threshold */
    encoder_->writeStep((int32_t) 1); /* Set the step to 1*/
    encoder_->writeInterruptConfig(0x00); /* Disable all the interrupt */
    encoder_->writeAntibouncingPeriod(20); /* Set an anti-bouncing of 200ms */
    encoder_->writeDoublePushPeriod(50); /*Set a period for the double push of 500ms */

}

void Encoder::update() {
    delta_ = 0;
    wasPressed_ = false;
    wasDoublePressed_ = false;
    wasLongPressed_ = false;

    if (encoder_->updateStatus()) {
        if (encoder_->readStatus(i2cEncoderLibV2::RINC)) {
            delta_ = 1;
            Serial.println("Encoder: Increment detected");
        }
        if (encoder_->readStatus(i2cEncoderLibV2::RDEC)) {
            delta_ = -1;
            Serial.println("Encoder: Decrement detected");
        }
        if (encoder_->readStatus(i2cEncoderLibV2::PUSHD)) {
            wasDoublePressed_ = true;
            Serial.println("Encoder: double press detected");
        }

        const bool pressedNow = encoder_->readStatus(i2cEncoderLibV2::PUSHP);
        const bool releasedNow = encoder_->readStatus(i2cEncoderLibV2::PUSHR);

        if (pressedNow && !buttonHeld_) {
            buttonHeld_ = true;
            holdStartMs_ = millis();
            longPressTriggered_ = false;
            wasPressed_ = true;
            Serial.println("Encoder: button press detected");
        } else if (releasedNow && buttonHeld_) {
            buttonHeld_ = false;
            longPressTriggered_ = false;
            holdStartMs_ = 0;
            Serial.println("Encoder: button released");
        }

        if (buttonHeld_ && !longPressTriggered_) {
            const uint32_t now = millis();
            if (now - holdStartMs_ >= Config::ENCODER_LONG_PRESS_THRESHOLD_MS) {
                wasLongPressed_ = true;
                longPressTriggered_ = true;
                wasPressed_ = false;  // Reset wasPressed_ to avoid triggering both press and long press
                Serial.println("Encoder: long press detected");
            }
        }
    }
}

int8_t Encoder::getDelta() const { return delta_; }
bool Encoder::wasPressed() const { return wasPressed_; }
bool Encoder::wasDoublePressed() const { return wasDoublePressed_; }
bool Encoder::wasLongPressed() const { return wasLongPressed_; }
bool Encoder::consumeLongPress() {
    if (!wasLongPressed_) {
        return false;
    }
    wasLongPressed_ = false;
    return true;
}

}  // namespace variometer
