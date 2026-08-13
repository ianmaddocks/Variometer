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

private:
    i2cEncoderLibV2* encoder_;
    int8_t delta_ = 0;
    bool wasPressed_ = false;
    bool wasDoublePressed_ = false;
};

}  // namespace variometer
