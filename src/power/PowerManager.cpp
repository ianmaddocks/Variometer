#include "power/PowerManager.h"
#include <Arduino.h>
#include <esp_sleep.h>

#include "config/Config.h"

namespace variometer {

void PowerManager::begin() {}

bool PowerManager::shouldPowerOff() const { return powerOffRequested_; }

void PowerManager::requestPowerOff() {
    if (!powerOffRequested_) {
        powerOffRequested_ = true;
        requestedAtMs_ = millis();
    }
}

bool PowerManager::readyToSleep() const {
    if (!powerOffRequested_) {
        return false;
    }

    // Let the power-off tune finish before cutting the CPU.
    return millis() - requestedAtMs_ >= Config::POWER_OFF_TUNE_DELAY_MS;
}

void PowerManager::sleepNow() {
    DBGLN("PowerManager: entering deep sleep");

    // No wake source is configured here deliberately: this is a
    // deliberate user-initiated power-off, not a timed nap, so the
    // device should stay off until physically power-cycled/woken.
    esp_deep_sleep_start();
}

}  // namespace variometer
