#pragma once

#include <cstdint>

namespace variometer {

class PowerManager {
public:
    PowerManager() = default;
    void begin();
    bool shouldPowerOff() const;
    void requestPowerOff();
    void update();

private:
    bool powerOffRequested_ = false;
    uint32_t requestedAtMs_ = 0;
};

}  // namespace variometer
