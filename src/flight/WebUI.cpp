#include "flight/WebUI.h"

#include <LittleFS.h>
// #include <Update.h>  // OTA disabled -- see partitions.csv comment for why.
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

/*
 * Symbols for the embedded static pages (see board_build.embed_txtfiles
 * in platformio.ini) -- linked in directly from src/web/style.css and
 * src/web/vario.html, not read from a filesystem at runtime. The
 * asm() name is the linker section PlatformIO's embed step generates:
 * "_binary_" + the path as given to embed_txtfiles with every non-
 * alphanumeric character replaced by "_", + "_start"/"_end". Content is
 * NUL-terminated, so the single-argument send_P() overload (which uses
 * strlen_P()) works the same way the old inline PROGMEM strings did.
 */
extern const uint8_t style_css_start[] asm("_binary_src_web_style_css_start");
extern const uint8_t vario_html_start[] asm("_binary_src_web_vario_html_start");

}  // namespace

void WebUI::begin() {
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
    IPAddress apAddress;
    if (!apAddress.fromString(Config::WIFI_AP_IP_ADDRESS) ||
        !WiFi.softAPConfig(apAddress, apAddress, IPAddress(255, 255, 255, 0))) {
        DBGLN("WiFi AP address configuration failed");
    }
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
    webServer.on("/", HTTP_GET, [this]() { handleVarioPage(); });
    webServer.on("/style.css", HTTP_GET, [this]() { handleStyle(); });
    webServer.on("/status", HTTP_GET, [this]() { handleStatusJson(); });
    webServer.on("/toggle_recording", HTTP_POST, [this]() { handleToggleRecording(); });

    webServer.on("/flights", HTTP_GET, [this]() { handleFlightsPage(); });
    webServer.on("/download", HTTP_GET, [this]() { handleDownload(); });
    webServer.on("/delete", HTTP_GET, [this]() { handleDeleteLog(); });

    webServer.on("/settings", HTTP_GET, [this]() { handleSettingsPage(); });
    webServer.on("/settings/save", HTTP_POST, [this]() { handleSettingsSave(); });
    /*
     * OTA firmware upload disabled: this project uses partitions.csv's
     * single-app-partition layout now, which does not reserve a second
     * OTA slot for Update.begin()/end() to flash into. Re-enabling this
     * route requires reverting partitions.csv to a two-slot ota_0/ota_1
     * scheme first, which halves the usable app flash again.
     */
    // webServer.on("/update", HTTP_POST,
    //              [this]() {
    //                  webServer.send(200, "text/plain", Update.hasError() ? "Firmware update failed" : "Update complete; rebooting");
    //                  if (!Update.hasError()) {
    //                      delay(100);
    //                      ESP.restart();
    //                  }
    //              },
    //              [this]() { handleFirmwareUpload(); });
    webServer.onNotFound([]() {
        webServer.sendHeader("Location", "/");
        webServer.send(302, "text/plain", "");
    });
    webServer.begin();
    dnsServer_.start(53, "*", WiFi.softAPIP());

    DBGF("WiFi AP ready: %s, address %s\n", Config::WIFI_AP_SSID,
         WiFi.softAPIP().toString().c_str());
}

void WebUI::stopNetwork() {
    if (server_ != nullptr) {
        webServer.stop();
        server_ = nullptr;
    }

    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    dnsServer_.stop();

    DBGLN("WebUI: WiFi AP stopped");
}

void WebUI::update() {
    if (server_ != nullptr) {
        server_->handleClient();
        dnsServer_.processNextRequest();
    }
    if (active_ && millis() - lastFlushMs_ >= Config::FLIGHT_LOG_FLUSH_INTERVAL_MS) {
        activeFile_.flush();
        lastFlushMs_ = millis();
    }
}

bool WebUI::startFlight(const GPS::DateTime& startTime) {
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

void WebUI::appendPoint(const LogSample& point) {
    if (!active_) {
        return;
    }
    activeFile_.printf("%.3f,%.3f,%.7f,%.7f,%.1f,%u,%u,%.2f,%.2f\n", point.timeSeconds,
                       point.altitude, point.latitude, point.longitude, point.groundSpeedKmh,
                       static_cast<unsigned>(point.satellites), point.gpsFix ? 1 : 0,
                       point.pressureHpa, point.temperatureC);
}

void WebUI::finishFlight(uint32_t durationSeconds) {
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

bool WebUI::isActive() const { return active_; }

bool WebUI::consumeSettingsChanged() {
    if (!settingsChanged_) {
        return false;
    }
    settingsChanged_ = false;
    return true;
}

bool WebUI::consumeRecordToggleRequest() {
    if (!recordToggleRequested_) {
        return false;
    }
    recordToggleRequested_ = false;
    return true;
}

String WebUI::makeStartName(const GPS::DateTime& startTime) const {
    if (!startTime.valid) {
        return "00000000-000000";
    }
    char name[32];
    snprintf(name, sizeof(name), "%04u%02u%02u-%02u%02u%02u",
             startTime.year, startTime.month, startTime.day, startTime.hour,
             startTime.minute, startTime.second);
    return String(name);
}

bool WebUI::isSafeFileName(const String& name) const {
    if (name.length() == 0 || name.length() > 64 || name.indexOf("..") >= 0 ||
        name.indexOf('/') >= 0 || name.indexOf('\\') >= 0) {
        return false;
    }
    return name.endsWith(".csv") || name.endsWith(".part");
}

void WebUI::handleVarioPage() {
    webServer.send_P(200, "text/html", (PGM_P)vario_html_start);
}

void WebUI::handleStyle() {
    webServer.send_P(200, "text/css", (PGM_P)style_css_start);
}

void WebUI::handleStatusJson() {
    if (flightData_ == nullptr) {
        webServer.send(503, "application/json", "{}");
        return;
    }
    const FlightData& data = *flightData_;

    String json = "{";
    json += "\"recording\":"; json += (data.recordingActive ? "true" : "false"); json += ",";
    json += "\"elapsedS\":"; json += data.recordingDurationS; json += ",";
    json += "\"vsMps\":"; json += String(data.verticalSpeed, 2); json += ",";
    json += "\"vsAvgMps\":"; json += String(data.verticalSpeedAverage30s, 2); json += ",";
    json += "\"altitude\":"; json += String(data.relativeAltitude, 1); json += ",";
    json += "\"gpsFix\":"; json += (data.gpsFix ? "true" : "false"); json += ",";
    json += "\"sats\":"; json += static_cast<int>(data.satellites); json += ",";
    json += "\"lat\":"; json += String(data.latitude, 6); json += ",";
    json += "\"lon\":"; json += String(data.longitude, 6); json += ",";
    json += "\"speedKmh\":"; json += String(data.groundSpeed * 3.6f, 1); json += ",";
    json += "\"track\":"; json += String(data.track, 1); json += ",";
    json += "\"batteryPercent\":"; json += String(data.batteryPercent, 0); json += ",";
    json += "\"batteryVoltage\":"; json += String(data.batteryVoltage, 2);
    json += "}";
    webServer.send(200, "application/json", json);
}

void WebUI::handleToggleRecording() {
    recordToggleRequested_ = true;
    webServer.send(200, "text/plain", "OK");
}

void WebUI::handleDownload() {
    if (!mounted_) {
        webServer.send(503, "text/plain", "Flight-log storage unavailable");
        return;
    }
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
    // Disable Nagle on this connection: batched with delayed ACK on the
    // client side, Nagle can add tens of ms of stall per write, which
    // streamFile()'s many small chunks turn into a slow trickle.
    webServer.client().setNoDelay(true);
    webServer.streamFile(file, "text/csv");
    file.close();
}

void WebUI::handleDeleteLog() {
    if (!mounted_) {
        webServer.send(503, "text/plain", "Flight-log storage unavailable");
        return;
    }
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
}

void WebUI::handleFlightsPage() {
    String page;
    // Sized generously for a typical log count so the per-row
    // concatenation below mostly appends into existing capacity rather
    // than reallocating and copying the whole page on every row.
    page.reserve(2048);
    page = "<!doctype html><html><head><meta charset=\"utf-8\">"
           "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,maximum-scale=1\">"
           "<title>Flights</title><link rel=\"stylesheet\" href=\"/style.css\"></head><body>"
           "<div class=\"page\"><h1>Flights</h1><div class=\"card\"><ul class=\"filelist\">";

    bool any = false;
    if (mounted_) {
        File directory = LittleFS.open("/flights", "r");
        if (directory) {
            File file = directory.openNextFile();
            while (file) {
                const String fullName = String(file.name());
                const String name = fullName.substring(fullName.lastIndexOf('/') + 1);
                if (name.endsWith(".csv") || name.endsWith(".part")) {
                    any = true;
                    page += "<li id='log-" + htmlEscape(name) + "'><a href='/download?name=" +
                            htmlEscape(name) + "'>" + htmlEscape(name) + "</a><span><span class='size'>" +
                            String(file.size()) + " B</span> "
                            "<button type='button' class='rm' onclick=\"removeLog(this,'" + htmlEscape(name) +
                            "')\">Delete</button></span></li>";
                }
                file = directory.openNextFile();
            }
            directory.close();
        }
    }
    if (!any) {
        page += "<li>No recorded flights yet.</li>";
    }

    page += "</ul></div></div>"
            "<nav class=\"tabs\"><a href=\"/\">Vario</a><a href=\"/flights\" class=\"active\">Flights</a><a href=\"/settings\">Settings</a></nav>"
            "<script>"
            "function removeLog(btn,name){"
            "if(!confirm('Delete this log?'))return;"
            "btn.disabled=true;btn.textContent='...';"
            "fetch('/delete?name='+encodeURIComponent(name)).then(function(r){"
            "if(!r.ok)throw 0;"
            "document.getElementById('log-'+name).remove();"
            "}).catch(function(){btn.disabled=false;btn.textContent='Delete';alert('Delete failed');});"
            "}"
            "</script></body></html>";
    webServer.send(200, "text/html", page);
}

void WebUI::handleSettingsPage() {
    String html = "<!doctype html><html><head><meta charset=\"utf-8\">"
                  "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,maximum-scale=1\">"
                  "<title>Settings</title><link rel=\"stylesheet\" href=\"/style.css\"></head><body>"
                  "<div class=\"page\"><h1>Settings</h1>";

    if (settings_ == nullptr) {
        html += "<div class=\"card\">Settings unavailable.</div>";
    } else {
        html += "<form class=\"card\" method=\"POST\" action=\"/settings/save\">"
                "<div class=\"toggle-row\"><span>Audio vario</span>"
                "<input type=\"checkbox\" name=\"audio\"";
        html += settings_->audioVarioEnabled ? " checked" : "";
        html += "></div>"
                "<div class=\"toggle-row\"><span>Haptic vario</span>"
                "<input type=\"checkbox\" name=\"haptic\"";
        html += settings_->hapticVarioEnabled ? " checked" : "";
        html += "></div>"
                "<label for=\"replay\">3D replay speed (x real time)</label>"
                "<input type=\"number\" id=\"replay\" name=\"replay\" min=\"1\" max=\"10\" value=\"";
        html += String(settings_->replaySpeed);
        html += "\">"
                "<label for=\"minsat\">Min satellites to detect takeoff</label>"
                "<input type=\"number\" id=\"minsat\" name=\"minsat\" min=\"3\" max=\"12\" value=\"";
        html += String(settings_->minSatellites);
        html += "\">"
                "<div class=\"toggle-row\"><span>Invert display (white background)</span>"
                "<input type=\"checkbox\" name=\"invert\"";
        html += settings_->backgroundWhite ? " checked" : "";
        html += "></div>"
                "<div style=\"margin-top:16px;\"><input type=\"submit\" value=\"Save Settings\"></div>"
                "</form>";
    }

    // Firmware Update card removed along with the /update route -- see
    // the comment above that route's registration for why.
    // html += "<div class=\"card\"><label style=\"margin-top:0;\">Firmware Update</label>"
    //         "<form method=\"POST\" action=\"/update\" enctype=\"multipart/form-data\">"
    //         "<input type=\"file\" name=\"firmware\" accept=\".bin\" required>"
    //         "<div style=\"margin-top:12px;\"><input type=\"submit\" value=\"Upload &amp; Flash\"></div>"
    //         "</form><div class=\"hint\">Uploads a compiled .bin and reboots the device once flashed.</div></div>";

    html += "</div><nav class=\"tabs\"><a href=\"/\">Vario</a><a href=\"/flights\">Flights</a><a href=\"/settings\" class=\"active\">Settings</a></nav>"
            "</body></html>";
    webServer.send(200, "text/html", html);
}

void WebUI::handleSettingsSave() {
    if (settings_ == nullptr) {
        webServer.send(503, "text/plain", "Settings unavailable");
        return;
    }
    settings_->audioVarioEnabled = webServer.hasArg("audio");
    settings_->hapticVarioEnabled = webServer.hasArg("haptic");

    int replay = webServer.arg("replay").toInt();
    if (replay < 1) replay = 1;
    if (replay > 10) replay = 10;
    settings_->replaySpeed = static_cast<uint8_t>(replay);

    int minSat = webServer.arg("minsat").toInt();
    if (minSat < 3) minSat = 3;
    if (minSat > 12) minSat = 12;
    settings_->minSatellites = static_cast<uint8_t>(minSat);

    settings_->backgroundWhite = webServer.hasArg("invert");

    settingsChanged_ = true;

    webServer.sendHeader("Location", "/settings");
    webServer.send(303);
}

/*
void WebUI::handleFirmwareUpload() {
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
*/

}  // namespace variometer
