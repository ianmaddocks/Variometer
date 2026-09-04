#include "flight/FlightDetector.h"

#include <Arduino.h>

namespace variometer {
namespace {
constexpr uint32_t kTakeoffHoldMs = 2000;
constexpr uint32_t kLandingHoldMs = 5000;
constexpr float kTakeoffSpeedMps = 4.0f;
constexpr float kTakeoffClimbMps = 1.5f;
}  // namespace

void FlightDetector::update(const FlightData& data) {
    const uint32_t now = millis();

    switch (state_) {
        case FlightState::PREFLIGHT:
        case FlightState::POST_FLIGHT:
            if (data.gpsFix && data.satellites >= data.minSatellites && data.groundSpeed > kTakeoffSpeedMps) {//} &&
            //data.verticalSpeed > kTakeoffClimbMps) {
                if (!takeoffCandidate_) {
                    takeoffCandidate_ = true;
                    takeoffHoldMs_ = now;
                } else if (now - takeoffHoldMs_ >= kTakeoffHoldMs) {
                    state_ = FlightState::TAKEOFF_DETECTED;
                    takeoffCandidate_ = false;
                }
            } else {
                takeoffCandidate_ = false;
            }
            break;
        case FlightState::TAKEOFF_DETECTED:
            state_ = FlightState::FLIGHT;
            break;
        case FlightState::FLIGHT:
            if (data.gpsFix && data.groundSpeed < 1.2f ) {//&& fabsf(data.verticalSpeed) < 0.3f) {
                if (!landingCandidate_) {
                    landingCandidate_ = true;
                    landingHoldMs_ = now;
                } else if (now - landingHoldMs_ >= kLandingHoldMs) {
                    state_ = FlightState::LANDING_DETECTED;
                    landingCandidate_ = false;
                }
            } else {
                landingCandidate_ = false;
            }
            break;
        case FlightState::LANDING_DETECTED:
            state_ = FlightState::POST_FLIGHT;
            break;
    }
}

void FlightDetector::requestTakeoff() {
    if (state_ == FlightState::PREFLIGHT) {
        state_ = FlightState::TAKEOFF_DETECTED;
        takeoffCandidate_ = false;
    }
}

FlightState FlightDetector::getState() const { return state_; }

}  // namespace variometer
