#pragma once

#include <Arduino.h>

#define VARIOMETER_VERSION "v0.0.1"


#if defined(DEBUG) || !defined(NDEBUG)
  // DBG continues the current line (no timestamp); DBGLN/DBGF start a new line and are timestamped.
  #define DBG(...) do { Serial.printf("[%lu] ", millis()); Serial.print(__VA_ARGS__); } while (0)
  #define DBGLN(...) do { Serial.printf("[%lu] ", millis()); Serial.println(__VA_ARGS__); } while (0)
  #define DBGF(...) do { Serial.printf("[%lu] ", millis()); Serial.printf(__VA_ARGS__); } while (0)
#else
  #define DBG(...) do { } while (0)
  #define DBGLN(...) do { } while (0)
  #define DBGF(...) do { } while (0)
#endif

namespace Config {
constexpr int I2C_SDA = D9;
constexpr int I2C_SCL = D10;
constexpr int GPS_RX = D8;
constexpr int GPS_TX = D7;
constexpr uint32_t GPS_BAUD = 115200;
constexpr int BATTERY_PIN = A1;
constexpr int BUZZER_PIN = D0;
constexpr int HAPTIC_PIN = D2;
//constexpr uint32_t POWER_OFF_HOLD_MS = 2000;
constexpr float MAP_MAX_RANGE_KM = 5.0f;

constexpr uint8_t MIN_SATELLITES_DEFAULT = 5;
constexpr uint32_t GPS_UPDATE_INTERVAL_MS = 1000;

constexpr uint32_t DISPLAY_UPDATE_INTERVAL_MS = 100;
constexpr uint32_t ALTITUDE_TRACE_SAMPLE_INTERVAL_MS = 1000;
constexpr uint16_t FLIGHT_RECORDING_DURATION_MINUTES = 120;
constexpr uint32_t FLIGHT_LOG_FLUSH_INTERVAL_MS = 10000;
constexpr char WIFI_AP_SSID[] = "Variometer";
constexpr char WIFI_AP_PASSWORD[] = "";
constexpr uint8_t WIFI_AP_CHANNEL = 6;
constexpr uint32_t BATTERY_UPDATE_INTERVAL_MS = 5000;
constexpr float BATTERY_DIVIDER_RATIO = 2.0f;
constexpr float BATTERY_MIN_VOLTAGE = 3.2f;
constexpr float BATTERY_MAX_VOLTAGE = 4.2f;
constexpr float BATTERY_WARN_VOLTAGE = 3.6f;
constexpr float BATTERY_CRITICAL_VOLTAGE = 3.4f;
constexpr uint32_t BATTERY_SAMPLES = 8;
constexpr uint32_t LINE_SPACING = 9;
constexpr uint32_t ENCODER_I2C_ADDRESS = 0x01;
constexpr uint8_t ENCODER_DOUBLE_PRESS_PERIOD = 50;

constexpr uint32_t MS5611_UPDATE_INTERVAL_MS = 20;
constexpr uint32_t MS5611_UPDATE_INTERVAL_GPS = 1000;
constexpr uint32_t MS5611_DEBUG_INTERVAL_MS = 5000;
constexpr float VARIO_MAX_CLIMB = 5.0f;
constexpr float VARIO_MAX_SINK = -5.0f;
constexpr uint32_t VARIO_REGRESSION_WINDOW_MS = 300;
// Exponential smoothing.
// 0.25 gives reasonably quick response without excessive noise.
constexpr float VARIO_FILTER_ALPHA = 0.25f;

// Ignore very small vertical-speed values.
constexpr float VARIO_DEADBAND = 0.08f;

// Rate at which the vario decays toward zero inside the deadband.
constexpr float VARIO_ZERO_DECAY = 0.85f;

// Once below this value, force the result to exactly zero.
constexpr float VARIO_ZERO_THRESHOLD = 0.03f;

// Internal calculation limit.
// This is deliberately wider than the +/-5 m/s display range.
constexpr float VARIO_CALC_MAX = 20.0f;
}  // namespace Config
