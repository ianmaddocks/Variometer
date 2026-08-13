#pragma once

#include <Arduino.h>

namespace Config {
constexpr int I2C_SDA = D9;
constexpr int I2C_SCL = D10;
constexpr int GPS_RX = D8;
constexpr int GPS_TX = D7;
constexpr uint32_t GPS_BAUD = 115200;
constexpr int BATTERY_PIN = A1;
constexpr int BUZZER_PIN = D0;
constexpr int HAPTIC_PIN = D2;
constexpr uint32_t POWER_OFF_HOLD_MS = 2000;
constexpr float MAP_MAX_RANGE_KM = 5.0f;
constexpr float VARIO_MAX_CLIMB = 5.0f;
constexpr float VARIO_MAX_SINK = -5.0f;
constexpr uint8_t MIN_SATELLITES_DEFAULT = 5;
constexpr uint32_t GPS_UPDATE_INTERVAL_MS = 1000;
constexpr uint32_t MS5611_UPDATE_INTERVAL_MS = 20;
constexpr uint32_t DISPLAY_UPDATE_INTERVAL_MS = 100;
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
}  // namespace Config
