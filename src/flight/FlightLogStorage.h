#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <WebServer.h>

#include "flight/FlightRecorder.h"
#include "sensors/GPS.h"

namespace variometer {

class FlightLogStorage {
public:
    void begin();
    void update();
    bool startFlight(const GPS::DateTime& startTime);
    void appendPoint(const TracePoint& point);
    void finishFlight(uint32_t durationSeconds);
    bool isActive() const;

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