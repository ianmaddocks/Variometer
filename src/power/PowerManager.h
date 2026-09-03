#pragma once

#include <cstdint>

namespace variometer {

class PowerManager {
public:
    PowerManager() = default;
    void begin();
    bool shouldPowerOff() const;
    void requestPowerOff();

    // True once the power-off tune has had time to finish playing. The
    // caller is expected to shut down radios/the display and then call
    // sleepNow() -- this class does not do that itself so it stays
    // independent of BLE/WiFi/display, which would otherwise all need to
    // be wired into it.
    bool readyToSleep() const;

    // Cuts power via deep sleep. Never returns.
    void sleepNow();

private:
    bool powerOffRequested_ = false;
    uint32_t requestedAtMs_ = 0;
};

}  // namespace variometer
