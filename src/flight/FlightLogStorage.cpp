#include "flight/FlightLogStorage.h"

#include <LittleFS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config/Config.h"

namespace variometer {
namespace {

WebServer webServer(80);

String htmlEscape(const String& value) {
    String result = value;
    result.replace("&", "&amp;");
    result.replace("<", "&lt;");
    result.replace(">", "&gt;");
    result.replace("\"", "&quot;");
    return result;
}

}  // namespace

void FlightLogStorage::begin() {
    mounted_ = LittleFS.begin(false, "/littlefs", 10, "flights");
    if (!mounted_) {
        DBGLN("LittleFS mount failed; formatting the flight-log partition");
        mounted_ = LittleFS.begin(true, "/littlefs", 10, "flights");
    }

    if (!mounted_) {
        DBGLN("LittleFS unavailable; flight logs will not be persisted");
    } else {
        const bool directoryReady = LittleFS.exists("/flights") || LittleFS.mkdir("/flights");
        DBGF("LittleFS mounted: total=%lu used=%lu flights-directory=%s\n",
             static_cast<unsigned long>(LittleFS.totalBytes()),
             static_cast<unsigned long>(LittleFS.usedBytes()),
             directoryReady ? "ready" : "failed");
    }

    WiFi.mode(WIFI_AP);
    WiFi.softAP(Config::WIFI_AP_SSID, Config::WIFI_AP_PASSWORD, Config::WIFI_AP_CHANNEL);

    /*
     * Modem sleep is on by default and is the classic cause of an ESP32
     * softAP download that stalls for many seconds at a time despite the
     * file being tiny (see the 25KB-takes-a-minute report that prompted
     * this): the radio powers down between beacons/packets, so every
     * write from streamFile() below can sit queued for a full sleep
     * interval before it actually goes out. There is exactly one client
     * (the pilot's phone/laptop) and no other traffic to save power for,
     * so there is no upside to leaving sleep enabled here.
     */
    WiFi.setSleep(false);

    server_ = &webServer;
    webServer.on("/", HTTP_GET, [this]() { handleWebRequest(); });
    webServer.on("/download", HTTP_GET, [this]() { handleWebRequest(); });
    webServer.on("/delete", HTTP_GET, [this]() { handleWebRequest(); });
    webServer.on("/update", HTTP_POST,
                 [this]() {
                     webServer.send(200, "text/plain", Update.hasError() ? "Firmware update failed" : "Update complete; rebooting");
                     if (!Update.hasError()) {
                         delay(100);
                         ESP.restart();
                     }
                 },
                 [this]() { handleFirmwareUpload(); });
    webServer.begin();

    DBGF("WiFi AP ready: %s, address %s\n", Config::WIFI_AP_SSID,
         WiFi.softAPIP().toString().c_str());
}

void FlightLogStorage::stopNetwork() {
    if (server_ != nullptr) {
        webServer.stop();
        server_ = nullptr;
    }

    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);

    DBGLN("FlightLogStorage: WiFi AP stopped");
}

void FlightLogStorage::update() {
    if (server_ != nullptr) {
        server_->handleClient();
    }
    if (active_ && millis() - lastFlushMs_ >= Config::FLIGHT_LOG_FLUSH_INTERVAL_MS) {
        activeFile_.flush();
        lastFlushMs_ = millis();
    }
}

bool FlightLogStorage::startFlight(const GPS::DateTime& startTime) {
    if (!mounted_ || active_ || !LittleFS.exists("/flights")) {
        return false;
    }

    activePath_ = "/flights/" + makeStartName(startTime) + ".part";
    activeFile_ = LittleFS.open(activePath_, "w");
    if (!activeFile_) {
        activePath_ = "";
        return false;
    }
    activeFile_.println(
        "time_seconds,altitude_m,latitude,longitude,speed_kmh,satellites,gps_fix,pressure_hpa,temperature_c");
    activeFile_.flush();
    lastFlushMs_ = millis();
    active_ = true;
    return true;
}

void FlightLogStorage::appendPoint(const LogSample& point) {
    if (!active_) {
        return;
    }
    activeFile_.printf("%.3f,%.3f,%.7f,%.7f,%.1f,%u,%u,%.2f,%.2f\n", point.timeSeconds,
                       point.altitude, point.latitude, point.longitude, point.groundSpeedKmh,
                       static_cast<unsigned>(point.satellites), point.gpsFix ? 1 : 0,
                       point.pressureHpa, point.temperatureC);
}

void FlightLogStorage::finishFlight(uint32_t durationSeconds) {
    if (!active_) {
        return;
    }
    activeFile_.flush();
    activeFile_.close();

    const uint32_t durationMinutes = (durationSeconds + 30U) / 60U;
    char durationSuffix[16];
    snprintf(durationSuffix, sizeof(durationSuffix), "-%lu.csv",
             static_cast<unsigned long>(durationMinutes));
    String finalPath = activePath_;
    finalPath.replace(".part", durationSuffix);
    LittleFS.remove(finalPath);
    LittleFS.rename(activePath_, finalPath);
    activePath_ = "";
    active_ = false;
}

bool FlightLogStorage::isActive() const { return active_; }

String FlightLogStorage::makeStartName(const GPS::DateTime& startTime) const {
    if (!startTime.valid) {
        return "00000000-000000";
    }
    char name[32];
    snprintf(name, sizeof(name), "%04u%02u%02u-%02u%02u%02u",
             startTime.year, startTime.month, startTime.day, startTime.hour,
             startTime.minute, startTime.second);
    return String(name);
}

bool FlightLogStorage::isSafeFileName(const String& name) const {
    if (name.length() == 0 || name.length() > 64 || name.indexOf("..") >= 0 ||
        name.indexOf('/') >= 0 || name.indexOf('\\') >= 0) {
        return false;
    }
    return name.endsWith(".csv") || name.endsWith(".part");
}

void FlightLogStorage::handleWebRequest() {
    if (!mounted_) {
        webServer.send(503, "text/plain", "Flight-log storage unavailable");
        return;
    }

    if (webServer.uri() == "/download") {
        const String name = webServer.arg("name");
        if (!isSafeFileName(name)) {
            webServer.send(400, "text/plain", "Invalid log name");
            return;
        }
        const String path = "/flights/" + name;
        File file = LittleFS.open(path, "r");
        if (!file) {
            webServer.send(404, "text/plain", "Log not found");
            return;
        }
        // Disable Nagle on this connection: batched with delayed ACK on
        // the client side, Nagle can add tens of ms of stall per write,
        // which streamFile()'s many small chunks turn into a slow trickle.
        webServer.client().setNoDelay(true);
        webServer.streamFile(file, "text/csv");
        file.close();
        return;
    }

    if (webServer.uri() == "/delete") {
        const String name = webServer.arg("name");
        if (!isSafeFileName(name)) {
            webServer.send(400, "text/plain", "Invalid log name");
            return;
        }
        const String path = "/flights/" + name;
        if (active_ && path == activePath_) {
            webServer.send(409, "text/plain", "Cannot delete the flight log currently being recorded");
            return;
        }
        if (!LittleFS.remove(path)) {
            webServer.send(404, "text/plain", "Log not found");
            return;
        }
        webServer.send(200, "text/plain", "OK");
        return;
    }

    String page;
    // Sized generously for a typical log count so the per-row
    // concatenation below mostly appends into existing capacity rather
    // than reallocating and copying the whole page on every row.
    page.reserve(2048);
    page = "<!doctype html><html><head><meta name='viewport' content='width=device-width'>"
           "<title>Variometer</title></head><body><h1>Variometer</h1>"
           "<p>Wi-Fi: " + String(Config::WIFI_AP_SSID) + "</p><h2>Flight logs</h2><ul>";
    File directory = LittleFS.open("/flights", "r");
    if (directory) {
        File file = directory.openNextFile();
        while (file) {
            const String fullName = String(file.name());
            const String name = fullName.substring(fullName.lastIndexOf('/') + 1);
            if (name.endsWith(".csv") || name.endsWith(".part")) {
                page += "<li id='log-" + htmlEscape(name) + "'><a href='/download?name=" +
                        htmlEscape(name) + "'>" + htmlEscape(name) + "</a> (" +
                        String(file.size()) + " bytes) &nbsp; "
                        "<button type='button' onclick=\"removeLog(this,'" + htmlEscape(name) +
                        "')\">Remove</button></li>";
            }
            file = directory.openNextFile();
        }
        directory.close();
    }
    page += "</ul><h2>Firmware update</h2><form method='POST' action='/update' enctype='multipart/form-data'>"
            "<input type='file' name='firmware' accept='.bin' required><button type='submit'>Upload firmware</button>"
            "</form><script>"
            "function removeLog(btn,name){"
            "if(!confirm('Delete this log?'))return;"
            "btn.disabled=true;btn.textContent='Removing...';"
            "fetch('/delete?name='+encodeURIComponent(name)).then(function(r){"
            "if(!r.ok)throw 0;"
            "document.getElementById('log-'+name).remove();"
            "}).catch(function(){btn.disabled=false;btn.textContent='Remove';alert('Delete failed');});"
            "}"
            "</script></body></html>";
    webServer.send(200, "text/html", page);
}

void FlightLogStorage::handleFirmwareUpload() {
    HTTPUpload& upload = webServer.upload();
    if (upload.status == UPLOAD_FILE_START) {
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        // Guarded on isRunning() so a failed begin() (or a write that
        // already failed and aborted the update) doesn't keep feeding
        // bytes to an Update object that was never successfully started.
        if (Update.isRunning() &&
            Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.isRunning() && !Update.end(true)) {
            Update.printError(Serial);
        }
    }
}

}  // namespace variometer