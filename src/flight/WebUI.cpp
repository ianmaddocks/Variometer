#include "flight/WebUI.h"

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

const char PAGE_STYLE[] PROGMEM = R"rawliteral(
:root{--bg:#0b0d10;--fg:#e8e8e8;--accent:#3ddc84;--sink:#ff5252;--muted:#8a8f98;--card:#171a1f;}
*{box-sizing:border-box;}
html,body{margin:0;padding:0;background:var(--bg);color:var(--fg);font-family:-apple-system,Segoe UI,Roboto,sans-serif;}
body{padding-bottom:64px;}
.page{max-width:480px;margin:0 auto;padding:16px;}
h1{font-size:1.1rem;font-weight:600;color:var(--muted);text-transform:uppercase;letter-spacing:.08em;margin:0 0 12px;}
.card{background:var(--card);border-radius:14px;padding:16px;margin-bottom:14px;}
.big{font-size:3.2rem;font-weight:700;text-align:center;margin:8px 0;}
.climb{color:var(--accent);}
.sink{color:var(--sink);}
.avg{text-align:center;color:var(--muted);font-size:.95rem;margin-top:-4px;}
.bar-wrap{height:180px;width:56px;background:#20242b;border-radius:10px;margin:12px auto;position:relative;overflow:hidden;}
.bar{position:absolute;left:0;right:0;bottom:50%;background:var(--accent);}
.bar.sink{background:var(--sink);top:50%;bottom:auto;}
.center-line{position:absolute;left:0;right:0;top:50%;height:2px;background:#444;}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;}
.stat .label{color:var(--muted);font-size:.75rem;text-transform:uppercase;letter-spacing:.06em;}
.stat .value{font-size:1.4rem;font-weight:600;}
button,input[type=submit]{background:var(--accent);color:#04150a;border:none;border-radius:10px;padding:12px 16px;font-size:1rem;font-weight:600;width:100%;}
button.stop{background:var(--sink);color:#2a0000;}
label{display:block;color:var(--muted);font-size:.8rem;margin:12px 0 4px;}
.toggle-row{display:flex;align-items:center;justify-content:space-between;padding:8px 0;}
.toggle-row span{font-size:1rem;}
input[type=checkbox]{width:22px;height:22px;}
input[type=text],input[type=number],input[type=file]{width:100%;padding:10px;border-radius:8px;border:1px solid #2a2f37;background:#0f1216;color:var(--fg);font-size:1rem;}
.filelist{list-style:none;padding:0;margin:0;}
.filelist li{display:flex;justify-content:space-between;align-items:center;padding:10px 0;border-bottom:1px solid #23262c;}
.filelist a{color:var(--fg);text-decoration:none;word-break:break-all;}
.filelist .size{color:var(--muted);font-size:.8rem;margin-left:8px;white-space:nowrap;}
.rm{background:var(--sink);color:#2a0000;border:none;border-radius:6px;padding:6px 10px;font-size:.8rem;width:auto;}
.hint{color:var(--muted);font-size:.8rem;margin-top:6px;}
nav.tabs{position:fixed;bottom:0;left:0;right:0;display:flex;background:#14161a;border-top:1px solid #23262c;}
nav.tabs a{flex:1;text-align:center;padding:12px 0;color:var(--muted);text-decoration:none;font-size:.85rem;}
nav.tabs a.active{color:var(--accent);font-weight:600;}
)rawliteral";

const char PAGE_VARIO[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>Variometer</title><link rel="stylesheet" href="/style.css"></head><body>
<div class="page">
<h1>Vario</h1>
<div class="card">
<div class="big" id="vs">--</div>
<div class="avg" id="avg">avg --</div>
<div class="bar-wrap"><div class="center-line"></div><div class="bar" id="bar"></div></div>
</div>
<div class="card grid">
<div class="stat"><div class="label">Alt AGL</div><div class="value" id="alt">--</div></div>
<div class="stat"><div class="label">GPS Speed</div><div class="value" id="speed">--</div></div>
<div class="stat"><div class="label">Track</div><div class="value" id="track">--</div></div>
<div class="stat"><div class="label">Battery</div><div class="value" id="batt">--</div></div>
</div>
<div class="card">
<div class="stat"><div class="label">GPS</div><div class="value" id="pos" style="font-size:.95rem;">--</div></div>
</div>
<div class="card">
<div class="stat"><div class="label" id="recLabel">Idle</div><div class="value" id="recTime">00:00</div></div>
<button id="recBtn" onclick="toggleRec()">Start Recording</button>
</div>
</div>
<nav class="tabs"><a href="/" class="active">Vario</a><a href="/flights">Flights</a><a href="/settings">Settings</a></nav>
<script>
function fmt(n,d){return (Math.round(n*Math.pow(10,d))/Math.pow(10,d)).toFixed(d);}
function pad(n){return n<10?'0'+n:n;}
async function poll(){
 try{
  const r=await fetch('/status');const d=await r.json();
  const vs=d.vsMps;
  const vsEl=document.getElementById('vs');
  vsEl.textContent=(vs>=0?'+':'')+fmt(vs,1)+' m/s';
  vsEl.className='big '+(vs>=0?'climb':'sink');
  document.getElementById('avg').textContent='avg '+(d.vsAvgMps>=0?'+':'')+fmt(d.vsAvgMps,1)+' m/s';
  const bar=document.getElementById('bar');
  const pct=Math.max(-100,Math.min(100,vs/5*100));
  if(pct>=0){bar.className='bar';bar.style.height=Math.abs(pct)+'%';}
  else{bar.className='bar sink';bar.style.height=Math.abs(pct)+'%';}
  document.getElementById('alt').textContent=fmt(d.altitude,0)+' m';
  document.getElementById('speed').textContent=fmt(d.speedKmh,1)+' km/h';
  document.getElementById('track').textContent=fmt(d.track,0)+'°';
  document.getElementById('batt').textContent=fmt(d.batteryPercent,0)+'%';
  document.getElementById('pos').textContent=d.gpsFix?(fmt(d.lat,5)+', '+fmt(d.lon,5)+' • '+d.sats+' sats'):('no fix • '+d.sats+' sats');
  document.getElementById('recLabel').textContent=d.recording?'Recording':'Idle';
  const s=d.elapsedS||0;
  document.getElementById('recTime').textContent=pad(Math.floor(s/60))+':'+pad(s%60);
  const btn=document.getElementById('recBtn');
  btn.textContent=d.recording?'Stop Recording':'Start Recording';
  btn.className=d.recording?'stop':'';
 }catch(e){}
}
function toggleRec(){fetch('/toggle_recording',{method:'POST'}).then(poll);}
setInterval(poll,500);poll();
</script></body></html>
)rawliteral";

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
    webServer.on("/update", HTTP_POST,
                 [this]() {
                     webServer.send(200, "text/plain", Update.hasError() ? "Firmware update failed" : "Update complete; rebooting");
                     if (!Update.hasError()) {
                         delay(100);
                         ESP.restart();
                     }
                 },
                 [this]() { handleFirmwareUpload(); });
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
    webServer.send_P(200, "text/html", PAGE_VARIO);
}

void WebUI::handleStyle() {
    webServer.send_P(200, "text/css", PAGE_STYLE);
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

    html += "<div class=\"card\"><label style=\"margin-top:0;\">Firmware Update</label>"
            "<form method=\"POST\" action=\"/update\" enctype=\"multipart/form-data\">"
            "<input type=\"file\" name=\"firmware\" accept=\".bin\" required>"
            "<div style=\"margin-top:12px;\"><input type=\"submit\" value=\"Upload &amp; Flash\"></div>"
            "</form><div class=\"hint\">Uploads a compiled .bin and reboots the device once flashed.</div></div>";

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

}  // namespace variometer
