#pragma once

// Presentation-only unit conversion for the on-device Settings screen and
// the web UI. Every other subsystem -- VarioCalculator, FlightRecorder's
// CSV, BleTelemetry's LK8EX1/NMEA sentences -- keeps working in metric
// regardless of this setting; only the numbers actually drawn/rendered for
// a pilot go through here.

namespace variometer {
namespace units {

constexpr float KMH_TO_MPH = 0.621371192f;
constexpr float MS_TO_FTPS = 3.280839895f;
constexpr float M_TO_FT = 3.280839895f;

inline float speedForDisplay(float kmh, bool imperial) {
    return imperial ? kmh * KMH_TO_MPH : kmh;
}

// Used for both vertical speed (vario) and wind speed -- both are
// expressed in m/s in metric and convert the same way.
inline float metersPerSecondForDisplay(float metresPerSecond, bool imperial) {
    return imperial ? metresPerSecond * MS_TO_FTPS : metresPerSecond;
}

inline float altitudeForDisplay(float metres, bool imperial) {
    return imperial ? metres * M_TO_FT : metres;
}

inline const char* speedUnitLabel(bool imperial) {
    return imperial ? "mph" : "km/h";
}

inline const char* metersPerSecondUnitLabel(bool imperial) {
    return imperial ? "ft/s" : "m/s";
}

inline const char* altitudeUnitLabel(bool imperial) {
    return imperial ? "ft" : "m";
}

}  // namespace units
}  // namespace variometer
