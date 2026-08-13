#pragma once

#include "core/FlightData.h"

namespace variometer {

class FlightDetector {
public:
    FlightDetector() = default;
    void update(const FlightData& data);
    void requestTakeoff();
    FlightState getState() const;

private:
    FlightState state_ = FlightState::PREFLIGHT;
    uint32_t takeoffHoldMs_ = 0;
    uint32_t landingHoldMs_ = 0;
    bool takeoffCandidate_ = false;
    bool landingCandidate_ = false;
};

}  // namespace variometer
