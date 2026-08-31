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

/*
 * I2C bus speed.
 *
 * This MUST be set explicitly. The ESP32 Arduino core defaults Wire to
 * 100 kHz, which is far too slow for a 128x128 OLED:
 *
 *   128 x 128 pixels / 8 = 2048 bytes of framebuffer
 *   2048 bytes x 9 bit-times (8 data + 1 ACK) = 18432 bit-times
 *   18432 / 100000 Hz  = ~184 ms for a single full-screen refresh
 *   18432 / 400000 Hz  =  ~46 ms for a single full-screen refresh
 *
 * At 100 kHz a full redraw took longer than DISPLAY_UPDATE_INTERVAL_MS,
 * so the main loop was permanently saturated by display traffic. Because
 * every other subsystem is polled once per loop pass, that starved the
 * barometer down to roughly 2.6 samples/second and made the vario erratic.
 * See VarioCalculator.cpp for why sample rate mattered so much.
 *
 * 400 kHz (I2C "fast mode") is supported by all three devices on this
 * bus: biometric sensor, SH1107 and the DuPPa encoder.
 */
constexpr uint32_t I2C_CLOCK_HZ = 400000;

/*
 * Per-device bus speeds.
 *
 * These two devices want opposite things, so the bus is re-clocked
 * around the display flush rather than run at one compromise speed.
 *
 * The MS5611 this originally shipped with proved unreliable at 400 kHz
 * on this wiring: address NACKs (Wire error 2) on conversion commands,
 * and multi-byte reads returning 0xFF in the low bytes -- which corrupted
 * both the calibration data and the temperature conversion, and a corrupt
 * temperature swings the computed altitude by up to ~2000 m. The biometric
 * sensor
 * that replaced it has not been characterized on this wiring, so the same
 * conservative 100 kHz sensor clock is kept until proven unnecessary.
 *
 * The SH1107 pushes 2048 bytes per refresh and is the only device that
 * genuinely needs the speed, so it keeps 400 kHz. setClock() only
 * rewrites the peripheral's timing registers and is cheap enough to do
 * ~10 times a second.
 *
 * If the sensor is still unreliable at 100 kHz the fault is electrical
 * (pull-ups, wire length, supply decoupling), not timing.
 */
constexpr uint32_t I2C_CLOCK_SENSORS_HZ = 100000;
constexpr uint32_t I2C_CLOCK_DISPLAY_HZ = 400000;
constexpr int GPS_RX = D5;
constexpr int GPS_TX = D4;
constexpr uint32_t GPS_BAUD = 115200;
constexpr int BATTERY_PIN = A1;
//constexpr int BUZZER_PIN = D0;
constexpr int HAPTIC_PIN = D0;
constexpr int SW1_PIN = D6;
constexpr int SW2_PIN = D7;

// Debounce window for SW1/SW2, which unlike the encoder have no
// hardware antibouncing of their own.
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;

constexpr uint32_t POWER_OFF_HOLD_MS = 2000;

// Delay between a confirmed power-off request and actually entering deep
// sleep, so the power-off tune has time to finish playing.
constexpr uint32_t POWER_OFF_TUNE_DELAY_MS = 800;
constexpr float MAP_MAX_RANGE_KM = 5.0f;

constexpr uint8_t MIN_SATELLITES_DEFAULT = 5;

// Nominal fix rate of the receiver itself (informational).
constexpr uint32_t GPS_UPDATE_INTERVAL_MS = 1000;

/*
 * Size of the UART receive buffer used to hold incoming NMEA.
 *
 * At 115200 baud the M10 produces ~11.5 kB/second. The Arduino default
 * RX buffer is only 256 bytes, so anything longer than ~22 ms between
 * drains overflows and shreds sentences mid-line. We now drain the port
 * every loop pass (see Application::updateSensors) but keep a generous
 * buffer so a slow display refresh cannot cost us a fix.
 */
constexpr size_t GPS_RX_BUFFER_BYTES = 2048;

constexpr uint32_t DISPLAY_UPDATE_INTERVAL_MS = 100;
constexpr uint32_t ALTITUDE_TRACE_SAMPLE_INTERVAL_MS = 1000;

/*
 * How often a position is offered to the flight-map track.
 *
 * Faster than the altitude trace so turns are captured cleanly, but the
 * track applies its own distance filter, so this is an upper bound on
 * sampling rather than the storage rate.
 */
constexpr uint32_t FLIGHT_TRACK_SAMPLE_INTERVAL_MS = 250;

/*
 * Dead reckoning, used by FlightTrack to keep the map's "you are here"
 * marker moving during a GPS dropout.
 *
 * TIMEOUT is how long a straight-line, constant-speed extrapolation from
 * the last known velocity is trusted. Wind, terrain and pilot input all
 * make that assumption degrade quickly, so this is deliberately short --
 * long enough to ride out a typical multipath dropout under a canopy or
 * near terrain, short enough that the marker stops rather than wandering
 * arbitrarily far from the truth.
 *
 * LIVE_GRACE_MS is the small gap treated as normal polling latency
 * rather than an actual dropout, so the marker is not flagged as
 * dead-reckoned on every routine sample interval.
 */
constexpr uint32_t DEAD_RECKONING_TIMEOUT_MS = 60000;
constexpr uint32_t DEAD_RECKONING_LIVE_GRACE_MS = 2 * FLIGHT_TRACK_SAMPLE_INTERVAL_MS;

/*
 * Wind-from-circling estimator (see WindEstimator.h for the technique).
 *
 * WINDOW_MS must comfortably exceed one full turn. A paramotor/paraglider
 * thermalling circle typically takes 15-30s; 45s gives margin without
 * making the estimate sluggish to respond when the pilot moves to a new
 * thermal with different wind.
 *
 * MIN_SPEED_MS excludes samples too slow for heading to mean anything
 * (ground handling, just after touchdown) from ever entering the
 * history.
 *
 * MAX_MS is a sanity clamp on the output, not a physical limit -- it
 * exists so a burst of GPS noise cannot report a nonsense wind speed.
 */
constexpr uint32_t WIND_ESTIMATE_WINDOW_MS = 45000;
constexpr float WIND_ESTIMATE_MIN_SPEED_MS = 3.0f;
constexpr float WIND_ESTIMATE_MAX_MS = 25.0f;
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

constexpr uint32_t BIOMETRIC_SENSOR_UPDATE_INTERVAL_MS = 20;
constexpr uint32_t BIOMETRIC_SENSOR_DEBUG_INTERVAL_MS = 5000;

/*
 * Interval for the consolidated HEALTH line.
 *
 * Reports rates and counters rather than individual events. Per-event
 * logging of a fault that recurs every few hundred ms costs ~10 ms of
 * blocking serial write each time at 115200 baud, which perturbs the
 * very loop timing being measured.
 */
constexpr uint32_t HEALTH_REPORT_INTERVAL_MS = 1000;
constexpr uint32_t STATE_LOG_INTERVAL_MS = 1000;

/*
 * Per-sample vario trace.
 *
 * The HEALTH line reports rates, which cannot show *why* a reading is
 * erratic. This logs each accepted sample so the altitude signal and the
 * derived slope can be inspected directly, which separates the two
 * candidate causes:
 *
 *   alt jumps around        -> the sensor or bus is the problem
 *   alt smooth, raw swings  -> the regression or its timing is
 *
 * Off by default: at ~25 Hz this is far too much serial traffic to run
 * continuously, and the writes themselves would perturb loop timing.
 * Enable deliberately for a short bench capture, ideally with the device
 * stationary, then turn it back off.
 */
constexpr bool VARIO_TRACE_ENABLED = false;

// Log only every Nth accepted sample. 1 = every sample.
constexpr uint8_t VARIO_TRACE_DECIMATION = 2;
constexpr float VARIO_MAX_CLIMB = 5.0f;
constexpr float VARIO_MAX_SINK = -5.0f;

/*
 * Full-scale deflection of the graphical vario indicator, in m/s.
 *
 * Deliberately narrower than VARIO_MAX_CLIMB: the gauge is for reading
 * ordinary climb and sink at a glance, not for showing extremes. A wider
 * scale would squash the range a pilot actually cares about into a few
 * pixels, and would put the 0.5 m/s graduation marks so close together
 * that they stop being legible.
 *
 * Readings beyond this clamp at full deflection; the numeric value
 * beside the gauge still shows the true figure.
 */
constexpr float VARIO_DISPLAY_SCALE_MS = 3.0f;

// Graduation interval on the vario gauge, in m/s. Each boundary is drawn
// as a 1px unfilled gap so the fill can be read as a value, not just a
// bar length.
constexpr float VARIO_DISPLAY_GRADUATION_MS = 0.5f;

/*
 * VarioBarScreen tuning (screen-spec.md / the ported vario_screen.h/.cpp
 * design). Named separately from VARIO_DISPLAY_SCALE_MS above, which
 * belongs to WindDirectionScreen's twin-triangle gauge -- a different
 * screen with a deliberately different (narrower) scale, not the same
 * value under two names.
 */
// Full-scale deflection of the climb bar, in m/s. 5.0 matches the
// commercial convention for free flight (screen-spec.md). Do NOT
// auto-range this: a scale that rescales itself destroys the muscle
// memory the bar exists to build.
constexpr float VARIO_BAR_SCALE_MS = 5.0f;

// Below this sink rate the numeric block inverts to black-on-white --
// the mono equivalent of a red needle.
constexpr float VARIO_BAR_INVERT_BELOW_MS = -1.5f;

// Readings inside +/- this are shown as exactly zero, so the bar and
// the sign glyph don't flicker in still air.
constexpr float VARIO_BAR_DEAD_BAND_MS = 0.05f;

/*
 * 30-second average vario window (VarioCalculator).
 *
 * Sized so that once the ring buffer is full, the span between its
 * oldest and newest sample is as close to 30s as a 1 Hz sampling
 * cadence allows: N samples pushed exactly 1000ms apart span (N-1)
 * seconds, so 31 samples span 30s exactly.
 */
constexpr size_t VARIO_AVERAGE_HISTORY_SIZE = 31;
constexpr uint32_t VARIO_AVERAGE_SAMPLE_INTERVAL_MS = 1000;

/*
 * Length of the linear-regression window used to derive vertical speed.
 *
 * Regression slope noise falls off very fast as the window grows --
 * roughly as T^-1.5 -- so the window length matters far more than the
 * smoothing filter that follows it. Using the original MS5611's
 * datasheet noise at OSR 4096 (~0.1 m RMS of altitude) -- the biometric sensor
 * that replaced it has not been characterized on this wiring, so this
 * window length is carried forward unchanged pending real-world data:
 *
 *   300 ms window  @ 20 Hz  ->  ~0.47 m/s of noise on the raw slope
 *  1000 ms window  @ 25 Hz  ->  ~0.07 m/s of noise on the raw slope
 *
 * The old 300 ms window forced heavy post-filtering to stay readable,
 * and that filtering is what caused the sluggish-yet-jumpy response.
 * A longer window here is what lets VARIO_FILTER_TAU_MS stay short.
 *
 * The window MUST be long enough to hold the 3 samples the regression
 * requires. That is trivially true for the barometer at ~25 Hz, but GPS
 * altitude arrives roughly once per second (once per GGA sentence), so
 * a 1000 ms window would hold a single sample and the vario would sit at
 * zero forever. The GPS figure below is sized for at least 4 samples at
 * that rate, which also suits GPS altitude being far noisier: a long
 * baseline is exactly what a noisy, slow signal needs.
 */
#ifdef ALTITUDE_SOURCE_GPS
constexpr uint32_t VARIO_REGRESSION_WINDOW_MS = 8000;
#else
constexpr uint32_t VARIO_REGRESSION_WINDOW_MS = 1000;
#endif

/*
 * Minimum time between oldest and newest sample before a slope is
 * trusted. Guards against dividing a small altitude change by a very
 * short baseline just after a reset, which yields a huge false vario.
 */
constexpr uint32_t VARIO_MIN_REGRESSION_SPAN_MS = 250;

/*
 * Smoothing time constant applied after the regression.
 *
 * Expressed as a time constant rather than a fixed EMA alpha so the
 * filter behaves identically regardless of sample rate. The previous
 * fixed alpha of 0.25 silently changed meaning whenever loop timing
 * changed; at the intended 20 Hz it was equivalent to ~150 ms.
 *
 * Lower = snappier and noisier. Higher = smoother and laggier.
 */
constexpr uint32_t VARIO_FILTER_TAU_MS = 200;

// Ignore very small vertical-speed values.
constexpr float VARIO_DEADBAND = 0.08f;

/*
 * Time constant for decaying the reading toward zero inside the
 * deadband. Also expressed as a time constant for the reason above --
 * the old per-update multiply meant the decay rate depended entirely
 * on how often the loop happened to run.
 */
constexpr uint32_t VARIO_ZERO_DECAY_TAU_MS = 300;

// Once below this value, force the result to exactly zero.
constexpr float VARIO_ZERO_THRESHOLD = 0.03f;

// Internal calculation limit.
// This is deliberately wider than the +/-5 m/s display range.
constexpr float VARIO_CALC_MAX = 20.0f;

/*
 * BLE telemetry, for feeding a phone-based flight app (FlyGaggle) live
 * position/vario data.
 *
 * IMPORTANT CAVEAT: this targets the widely-used convention for
 * BLE-based vario/GPS accessories -- a GATT server exposing the Nordic
 * UART Service (NUS) that streams plain-text NMEA, specifically standard
 * $GPGGA/$GPRMC plus the $LK8EX1 vario sentence originated by LK8000 and
 * since adopted by XCTrack, FlySkyHy and others as their "generic
 * Bluetooth LE vario" input. This is NOT a confirmed FlyGaggle
 * specification -- verify against FlyGaggle's own external-instrument
 * pairing documentation, and adjust the service/characteristic UUIDs or
 * sentence set below if it expects something else.
 *
 * The device name is what the phone sees when scanning; it does not need
 * to match anything app-specific unless FlyGaggle filters by name.
 */
constexpr char BLE_DEVICE_NAME[] = "Variometer";

// GPS-derived sentences (GGA/RMC) at a conventional 1 Hz.
constexpr uint32_t BLE_GPS_SENTENCE_INTERVAL_MS = 1000;

// LK8EX1 (vario) sent faster than GPS position, since a paraglider vario
// needle/tone in the client app reads as laggy below a few Hz -- this
// follows what dedicated BLE vario accessories typically transmit at.
constexpr uint32_t BLE_VARIO_SENTENCE_INTERVAL_MS = 200;

// Nordic UART Service UUIDs -- fixed, standardised values, not
// project-specific. Do not change these unless deliberately moving away
// from NUS compatibility.
constexpr char BLE_NUS_SERVICE_UUID[] = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr char BLE_NUS_TX_CHARACTERISTIC_UUID[] = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr char BLE_NUS_RX_CHARACTERISTIC_UUID[] = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
}  // namespace Config
