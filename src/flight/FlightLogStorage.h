#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <WebServer.h>

#include "flight/FlightRecorder.h"
#include "sensors/GPS.h"

namespace variometer {

// Raw per-sample capture written to the CSV, richer than TracePoint (which
// only carries what the in-RAM altitude trace/chart needs). Populated
// straight from GPS + the barometer, independent of the flight-state
// machine's own altitude trace.
struct LogSample {
    float timeSeconds = 0.0f;
    float altitude = 0.0f;
    float latitude = 0.0f;
    float longitude = 0.0f;
    float groundSpeedKmh = 0.0f;
    uint8_t satellites = 0;
    bool gpsFix = false;
    float pressureHpa = 0.0f;
    float temperatureC = 0.0f;
};

class FlightLogStorage {
public:
    void begin();
    void update();
    bool startFlight(const GPS::DateTime& startTime);
    void appendPoint(const LogSample& point);
    void finishFlight(uint32_t durationSeconds);
    bool isActive() const;

    // Stops the download web server and the WiFi AP it runs on. Called
    // once during power-off shutdown, ahead of deep sleep.
    void stopNetwork();

private:
    void handleWebRequest();
    void handleFirmwareUpload();
    bool isSafeFileName(const String& name) const;
    String makeStartName(const GPS::DateTime& startTime) const;

    WebServer* server_ = nullptr;
    File activeFile_;
    String activePath_;
    uint32_t lastFlushMs_ = 0;
    bool mounted_ = false;
    bool active_ = false;
};

}  // namespace variometer