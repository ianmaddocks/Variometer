#include "display/WifiQrScreen.h"

#include <qrcode.h>

#include "config/Config.h"
#include "display/DisplayManager.h"

namespace variometer {
namespace {

constexpr uint8_t kQrVersion = 3;
constexpr uint8_t kQrModules = 29;
constexpr int16_t kModulePixels = 3;
constexpr int16_t kQuietZoneModules = 2;
constexpr int16_t kQrPixels = (kQrModules + 2 * kQuietZoneModules) * kModulePixels;
constexpr int16_t kQrX = (128 - kQrPixels) / 2;
constexpr int16_t kQrY = 14;
constexpr size_t kVersion3LowCapacity = 53;
constexpr size_t kQrBufferBytes = 110;

/*
 * SH1107 SETDISPLAYCLOCKDIV (0xD5) parameter byte: high nibble is
 * oscillator frequency, low nibble is clock divide ratio minus one.
 * Adafruit_SH1107::begin() leaves this at 0x51 (divide-by-2) for every
 * screen. A monochrome OLED doesn't light all its rows at once -- the
 * controller strobes them sequentially fast enough that persistence of
 * vision reads it as one solid image -- and that row-by-row scan is
 * exactly what a phone's rolling shutter can catch mid-cycle as banding
 * when photographing an otherwise-static frame, like this screen's QR
 * code. Dropping the divide ratio to 0 (divide-by-1, same oscillator
 * frequency) doubles the scan rate, halving how much of one scan a given
 * camera row-exposure sees -- the panel-side half of fixing that; the
 * other half is this screen only drawing once per visit rather than
 * re-pushing an identical frame every cycle (see needsRedraw() in the
 * header). Scoped to this screen alone (set in draw(), restored in
 * exit()) rather than changed globally in DisplayManager::begin(), since
 * every other screen only needs the vendor-tuned default.
 */
constexpr uint8_t kFastClockDiv = 0x50;
constexpr uint8_t kNormalClockDiv = 0x51;

void appendEscapedWifiField(String& payload, const char* value) {
    for (const char* character = value; *character != '\0'; ++character) {
        if (*character == '\\' || *character == ';' || *character == ',' ||
            *character == ':' || *character == '"') {
            payload += '\\';
        }
        payload += *character;
    }
}

bool makeWifiPayload(String& payload) {
    payload = "WIFI:T:";
    payload += Config::WIFI_AP_PASSWORD[0] == '\0' ? "nopass;S:" : "WPA;S:";
    appendEscapedWifiField(payload, Config::WIFI_AP_SSID);
    if (Config::WIFI_AP_PASSWORD[0] != '\0') {
        payload += ";P:";
        appendEscapedWifiField(payload, Config::WIFI_AP_PASSWORD);
    }
    payload += ";;";
    return payload.length() <= kVersion3LowCapacity;
}

}  // namespace

void WifiQrScreen::enter() {
    rendered_ = false;
    DBGLN("Entering WiFi QR screen");
}

void WifiQrScreen::update(const FlightData& data) {
    (void)data;
}

void WifiQrScreen::draw(DisplayManager& display, const FlightData& data) {
    (void)data;

    // Only ever called once per visit -- see needsRedraw() -- so whatever
    // this call draws (QR or an error message) is this visit's final
    // frame; mark it done up front so every return path below is covered.
    rendered_ = true;
    lastDisplay_ = &display;
    display.display().sendCommand(SH110X_SETDISPLAYCLOCKDIV, kFastClockDiv);

    String payload;
    if (!makeWifiPayload(payload)) {
        display.display().setTextSize(1);
        display.display().setCursor(0, 30);
        display.display().print("WiFi settings too long");
        return;
    }

    uint8_t modules[kQrBufferBytes];
    QRCode qrCode;
    if (qrcode_initText(&qrCode, modules, kQrVersion, ECC_LOW, payload.c_str()) != 0) {
        display.display().setTextSize(1);
        display.display().setCursor(0, 30);
        display.display().print("Unable to create QR");
        return;
    }

    // A white quiet zone around black modules is required for reliable
    // phone-camera detection against the OLED's otherwise black screen.
    display.display().fillRect(kQrX, kQrY, kQrPixels, kQrPixels, SH110X_WHITE);
    for (uint8_t row = 0; row < qrCode.size; ++row) {
        for (uint8_t column = 0; column < qrCode.size; ++column) {
            if (qrcode_getModule(&qrCode, column, row)) {
                const int16_t x = kQrX + (column + kQuietZoneModules) * kModulePixels;
                const int16_t y = kQrY + (row + kQuietZoneModules) * kModulePixels;
                display.display().fillRect(x, y, kModulePixels, kModulePixels, SH110X_BLACK);
            }
        }
    }
}

void WifiQrScreen::exit() {
    if (lastDisplay_ != nullptr) {
        lastDisplay_->display().sendCommand(SH110X_SETDISPLAYCLOCKDIV, kNormalClockDiv);
    }
}

}  // namespace variometer