#include "power/PowerManager.h"
#include <Arduino.h>

namespace variometer {

void PowerManager::begin() {}

bool PowerManager::shouldPowerOff() const { return powerOffRequested_; }

void PowerManager::requestPowerOff() {
    powerOffRequested_ = true;
}

void PowerManager::update() {}

}  // namespace variometer
