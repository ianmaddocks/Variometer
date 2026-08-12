#pragma once

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
};

}  // namespace variometer
