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
    DBGLN("Entering WiFi QR screen");
}

void WifiQrScreen::update(const FlightData& data) {
    (void)data;
}

void WifiQrScreen::draw(DisplayManager& display, const FlightData& data) {
    (void)data;

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

void WifiQrScreen::exit() {}

}  // namespace variometer