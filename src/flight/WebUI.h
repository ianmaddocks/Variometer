#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <WebServer.h>

#include "core/FlightData.h"
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

/*
 * Owns the WiFi AP and the device's whole web UI: the Vario/Flights/Settings
 * tabs, live status feed, flight-log CSV storage/download/delete, and the
 * OTA firmware upload. Kept as one class -- rather than splitting the web
 * server out on its own -- because it already owned the WebServer instance
 * for flight-log download before the other pages existed, and every page
 * here shares that one server and the same WiFi AP lifecycle.
 */
class WebUI {
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

    // Live data the Vario/Settings web pages read from. The pointer must
    // outlive this object (Application owns both for its lifetime).
    void setFlightData(const FlightData* data) { flightData_ = data; }

    // Settings the web Settings page edits directly. The pointer must
    // outlive this object.
    void setSettings(DeviceSettings* settings) { settings_ = settings; }

    // True once after a web settings save; the owner is expected to
    // persist and re-apply settings_ in response. Consuming clears it.
    bool consumeSettingsChanged();

    // True once after the web Vario page's start/stop recording button is
    // pressed; the owner is expected to act on it (same as an SW1 press).
    // Consuming clears it.
    bool consumeRecordToggleRequest();

private:
    void handleVarioPage();
    void handleStyle();
    void handleStatusJson();
    void handleToggleRecording();
    void handleFlightsPage();
    void handleDownload();
    void handleDeleteLog();
    void handleSettingsPage();
    void handleSettingsSave();
    void handleFirmwareUpload();
    bool isSafeFileName(const String& name) const;
    String makeStartName(const GPS::DateTime& startTime) const;

    WebServer* server_ = nullptr;
    DNSServer dnsServer_;
    File activeFile_;
    String activePath_;
    uint32_t lastFlushMs_ = 0;
    bool mounted_ = false;
    bool active_ = false;

    const FlightData* flightData_ = nullptr;
    DeviceSettings* settings_ = nullptr;
    bool settingsChanged_ = false;
    bool recordToggleRequested_ = false;
};

}  // namespace variometer
