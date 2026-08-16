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

void PowerManager::update() {
    if (!powerOffRequested_) {
        return;
    }

    // Let the power-off tune finish before cutting the CPU.
    if (millis() - requestedAtMs_ < Config::POWER_OFF_TUNE_DELAY_MS) {
        return;
    }

    DBGLN("PowerManager: entering deep sleep");

    // No wake source is configured here deliberately: this is a
    // deliberate user-initiated power-off, not a timed nap, so the
    // device should stay off until physically power-cycled/woken.
    // NOTE: flight-data persistence on power-off is not yet wired up
    // here; see FlightLogStorage::finishFlight(), which currently only
    // runs on the automatic landing-detection transition.
    esp_deep_sleep_start();
}

}  // namespace variometer
