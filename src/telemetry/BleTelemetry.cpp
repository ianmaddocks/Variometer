#include "telemetry/BleTelemetry.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <math.h>

#include "config/Config.h"

namespace variometer {
namespace {

/*
 * NMEA checksum: XOR of every byte between '$' and '*', printed as two
 * uppercase hex digits. Real client parsers (unlike this project's own
 * mock GPS feed, which never validates checksums) are likely to reject
 * a sentence with a wrong or missing one.
 */
uint8_t nmeaChecksum(const char* sentenceBody) {
    uint8_t checksum = 0;
    for (const char* p = sentenceBody; *p != '\0'; ++p) {
        checksum ^= static_cast<uint8_t>(*p);
    }
    return checksum;
}

// Appends "*CS\r\n" to a sentence body already written into buf, where
// CS is the checksum of buf's contents so far. Returns the total length.
size_t appendChecksumAndTerminate(char* buf, size_t bodyLen, size_t bufSize) {
    const uint8_t checksum = nmeaChecksum(buf);
    const int written = snprintf(buf + bodyLen, bufSize - bodyLen,
                                 "*%02X\r\n", checksum);
    return bodyLen + static_cast<size_t>(written > 0 ? written : 0);
}

/*
 * Latitude/longitude in NMEA's ddmm.mmmm / dddmm.mmmm format.
 *
 * Mirrors the format used throughout this project's mock GPS feed
 * (MockGpsFeed.cpp), just in the encode direction instead of decode.
 */
void formatNmeaLatitude(float latDeg, char* out, size_t outSize, char* hemisphere) {
    const float magnitude = fabsf(latDeg);
    const int degrees = static_cast<int>(magnitude);
    const float minutes = (magnitude - static_cast<float>(degrees)) * 60.0f;
    snprintf(out, outSize, "%02d%07.4f", degrees, static_cast<double>(minutes));
    *hemisphere = (latDeg < 0.0f) ? 'S' : 'N';
}

void formatNmeaLongitude(float lonDeg, char* out, size_t outSize, char* hemisphere) {
    const float magnitude = fabsf(lonDeg);
    const int degrees = static_cast<int>(magnitude);
    const float minutes = (magnitude - static_cast<float>(degrees)) * 60.0f;
    snprintf(out, outSize, "%03d%07.4f", degrees, static_cast<double>(minutes));
    *hemisphere = (lonDeg < 0.0f) ? 'W' : 'E';
}

}  // namespace

/*
 * NimBLE callback glue.
 *
 * Signature verified directly against the installed library (NimBLE-
 * Arduino 1.4.3, pinned in platformio.ini): NimBLEServerCallbacks offers
 * both a plain onConnect(NimBLEServer*) and an overload taking a
 * ble_gap_conn_desc*. The plain overload is all that's needed here, and
 * overriding it does not require also overriding the desc-taking one --
 * both have non-pure default (empty) implementations in the base class.
 */
class BleTelemetryServerCallbacks : public NimBLEServerCallbacks {
public:
    explicit BleTelemetryServerCallbacks(BleTelemetry* owner) : owner_(owner) {}

    void onConnect(NimBLEServer* server) override {
        (void)server;
        owner_->connected_ = true;
        DBGLN("BLE telemetry: central connected");
    }

    void onDisconnect(NimBLEServer* server) override {
        (void)server;
        owner_->connected_ = false;
        DBGLN("BLE telemetry: central disconnected, resuming advertising");
        // NimBLE does not auto-resume advertising after a disconnect.
        NimBLEDevice::startAdvertising();
    }

private:
    BleTelemetry* owner_;
};

void BleTelemetry::begin() {
    NimBLEDevice::init(Config::BLE_DEVICE_NAME);

    server_ = NimBLEDevice::createServer();
    server_->setCallbacks(new BleTelemetryServerCallbacks(this));

    NimBLEService* service = server_->createService(Config::BLE_NUS_SERVICE_UUID);

    txCharacteristic_ = service->createCharacteristic(
        Config::BLE_NUS_TX_CHARACTERISTIC_UUID, NIMBLE_PROPERTY::NOTIFY);

    /*
     * Created but never acted on. Some BLE central implementations
     * treat a service that only half-matches the expected NUS profile
     * (missing the write-side characteristic) as not a real match and
     * skip it, so this exists purely for profile completeness.
     */
    service->createCharacteristic(Config::BLE_NUS_RX_CHARACTERISTIC_UUID,
                                  NIMBLE_PROPERTY::WRITE);

    service->start();

    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(Config::BLE_NUS_SERVICE_UUID);
    advertising->start();

    DBGF("BLE telemetry: advertising as \"%s\"\n", Config::BLE_DEVICE_NAME);
}

bool BleTelemetry::isConnected() const {
    return connected_;
}

void BleTelemetry::update(const FlightData& data, const GPS::DateTime& utc,
                          bool baroValid, float pressurePa, float temperatureC) {
    if (!connected_) {
        return;
    }

    const uint32_t now = millis();

    if (now - lastGpsSentMs_ >= Config::BLE_GPS_SENTENCE_INTERVAL_MS) {
        sendGpsSentences(data, utc);
        lastGpsSentMs_ = now;
    }

    if (now - lastVarioSentMs_ >= Config::BLE_VARIO_SENTENCE_INTERVAL_MS) {
        sendVarioSentence(data, baroValid, pressurePa, temperatureC);
        lastVarioSentMs_ = now;
    }
}

void BleTelemetry::sendGpsSentences(const FlightData& data, const GPS::DateTime& utc) {
    char latStr[12];
    char lonStr[13];
    char latHemi, lonHemi;
    formatNmeaLatitude(data.latitude, latStr, sizeof(latStr), &latHemi);
    formatNmeaLongitude(data.longitude, lonStr, sizeof(lonStr), &lonHemi);

    // hhmmss.sss; a zeroed time is sent rather than withholding the
    // sentence if UTC has not been established yet.
    char timeStr[11];
    if (utc.valid) {
        snprintf(timeStr, sizeof(timeStr), "%02u%02u%02u.000",
                 utc.hour, utc.minute, utc.second);
    } else {
        snprintf(timeStr, sizeof(timeStr), "000000.000");
    }

    char buf[96];
    size_t len;

    // $GPGGA,time,lat,NS,lon,EW,fixQuality,numSats,hdop,alt,M,geoidSep,M,,
    len = static_cast<size_t>(snprintf(
        buf, sizeof(buf),
        "$GPGGA,%s,%s,%c,%s,%c,%d,%02u,1.0,%.1f,M,0.0,M,,",
        timeStr, latStr, latHemi, lonStr, lonHemi,
        data.gpsFix ? 1 : 0, static_cast<unsigned>(data.satellites),
        static_cast<double>(data.barometricAltitude)));
    len = appendChecksumAndTerminate(buf, len, sizeof(buf));
    txCharacteristic_->setValue(reinterpret_cast<uint8_t*>(buf), len);
    txCharacteristic_->notify();

    // ddmmyy for RMC; zeroed alongside the time if UTC is not valid.
    char dateStr[7];
    if (utc.valid) {
        snprintf(dateStr, sizeof(dateStr), "%02u%02u%02u",
                 utc.day, utc.month, static_cast<unsigned>(utc.year % 100));
    } else {
        snprintf(dateStr, sizeof(dateStr), "000000");
    }

    const float speedKnots = data.groundSpeed / 0.514444f;

    // $GPRMC,time,status,lat,NS,lon,EW,speedKn,course,date,,,mode
    len = static_cast<size_t>(snprintf(
        buf, sizeof(buf),
        "$GPRMC,%s,%c,%s,%c,%s,%c,%.1f,%.1f,%s,,,%c",
        timeStr, data.gpsFix ? 'A' : 'V', latStr, latHemi, lonStr, lonHemi,
        static_cast<double>(speedKnots), static_cast<double>(data.track),
        dateStr, data.gpsFix ? 'A' : 'N'));
    len = appendChecksumAndTerminate(buf, len, sizeof(buf));
    txCharacteristic_->setValue(reinterpret_cast<uint8_t*>(buf), len);
    txCharacteristic_->notify();
}

void BleTelemetry::sendVarioSentence(const FlightData& data, bool baroValid,
                                     float pressurePa, float temperatureC) {
    /*
     * $LK8EX1,pressure,altitude,vario,temperature,battery
     *
     * Field semantics and "no data" sentinels follow the commonly
     * documented LK8000 convention (pressure in Pa, altitude in m,
     * vario in cm/s, temperature in deg C, battery as a percentage).
     * This has not been verified against FlyGaggle's own parser --
     * if vario/altitude readings look wrong or absent in the app while
     * the connection itself is fine, this sentence's field meanings are
     * the first thing to re-check.
     */
    char buf[80];

    const int pressureField = baroValid
        ? static_cast<int>(pressurePa)
        : 999999;  // "no data" sentinel

    const int varioField = static_cast<int>(
        roundf(data.verticalSpeed * 100.0f));  // m/s -> cm/s

    const int temperatureField = baroValid
        ? static_cast<int>(roundf(temperatureC))
        : 99;  // "no data" sentinel

    const int batteryField = static_cast<int>(data.batteryPercent);

    size_t len = static_cast<size_t>(snprintf(
        buf, sizeof(buf),
        "$LK8EX1,%d,%.1f,%d,%d,%d",
        pressureField, static_cast<double>(data.barometricAltitude),
        varioField, temperatureField, batteryField));
    len = appendChecksumAndTerminate(buf, len, sizeof(buf));
    txCharacteristic_->setValue(reinterpret_cast<uint8_t*>(buf), len);
    txCharacteristic_->notify();
}

}  // namespace variometer
