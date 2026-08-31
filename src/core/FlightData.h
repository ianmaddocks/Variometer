#pragma once

#include <cstdint>

#include "config/Config.h"

namespace variometer {

enum class FlightState : uint8_t {
    PREFLIGHT,
    TAKEOFF_DETECTED,
    FLIGHT,
    LANDING_DETECTED,
    POST_FLIGHT
};

struct DeviceSettings {
    uint8_t minSatellites = Config::MIN_SATELLITES_DEFAULT;
    bool audioVarioEnabled = false;
    bool hapticVarioEnabled = true;
    bool backgroundWhite = false;
    uint16_t altitudeTraceMinutes = 30;
    uint8_t initialFlightScreen = 0;

    // Playback rate for the post-flight 3D replay, as a multiple of real
    // time. 3x replays a typical flight in a reviewable length of time.
    uint8_t replaySpeed = 3;
};

struct FlightData {
    float latitude = 0.0f;
    float longitude = 0.0f;
    float gpsAltitude = 0.0f;
    float barometricAltitude = 0.0f;
    float relativeAltitude = 0.0f;
    float verticalSpeed = 0.0f;

    // 30-second average vario (VarioCalculator), for VarioBarScreen's
    // dashed on-bar marker. 0.0f until at least two samples of the
    // averaging window have been collected (see VarioCalculator.cpp).
    float verticalSpeedAverage30s = 0.0f;

    float groundSpeed = 0.0f;
    float track = 0.0f;
    uint8_t satellites = 0;
    bool gpsFix = false;
    float batteryVoltage = 0.0f;
    float batteryPercent = 0.0f;
    uint32_t flightDuration = 0;
    float distanceFromLZ = 0.0f;

    // True/geographic bearing from the current position to the LZ, in
    // degrees (0-360, 0=north). 0.0f and meaningless whenever hasLz is
    // false -- always check hasLz, not this value, to detect "no LZ yet".
    float bearingToLZ = 0.0f;

    float lzLatitude = 0.0f;
    float lzLongitude = 0.0f;
    bool hasLz = false;
    float windSpeed = 0.0f;
    float windDirection = 0.0f;
    float windConfidence = 0.0f;
    uint16_t tracePointCount = 0;
    float traceAltitudeMin = 0.0f;
    float traceAltitudeMax = 0.0f;
    float traceAltitudeSpan = 0.0f;
    FlightState flightState = FlightState::PREFLIGHT;
};

}  // namespace variometer
