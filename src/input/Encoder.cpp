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
    encoder_->writeDoublePushPeriod(Config::ENCODER_DOUBLE_PRESS_PERIOD);

}

void Encoder::update() {
    delta_ = 0;
    wasPressed_ = false;
    wasDoublePressed_ = false;

    if (encoder_->updateStatus()) {
        if (encoder_->readStatus(i2cEncoderLibV2::RINC)) delta_ = 1;
        if (encoder_->readStatus(i2cEncoderLibV2::RDEC)) delta_ = -1;
        if (encoder_->readStatus(i2cEncoderLibV2::PUSHP)) wasPressed_ = true;
        if (encoder_->readStatus(i2cEncoderLibV2::PUSHD)) wasDoublePressed_ = true;

        DBG("Encoder status: ");
        if (delta_ != 0) DBG("Delta: " + String(delta_) + " ");
        if (wasPressed_) DBG("Pressed ");
        if (wasDoublePressed_) DBG("Double Pressed ");
        DBGLN("");
    }
}

int8_t Encoder::getDelta() const { return delta_; }
bool Encoder::wasPressed() const { return wasPressed_; }
bool Encoder::wasDoublePressed() const { return wasDoublePressed_; }
bool Encoder::consumeDoublePress() {
    if (!wasDoublePressed_) {
        return false;
    }
    wasDoublePressed_ = false;
    return true;
}

}  // namespace variometer
