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
 *
 * sentenceBody must point PAST the leading '$' -- the '$' itself is not
 * part of the checksum. (Previously this was called with a pointer to
 * the '$', XORing it into every checksum and making every emitted
 * sentence fail validation against any conforming parser.)
 */
uint8_t nmeaChecksum(const char* sentenceBody) {
    uint8_t checksum = 0;
    for (const char* p = sentenceBody; *p != '\0'; ++p) {
        checksum ^= static_cast<uint8_t>(*p);
    }
    return checksum;
}

/*
 * Appends "*CS\r\n" to a sentence body already written into buf (which
 * must start with '$'), where CS is the checksum of the bytes strictly
 * between '$' and this point. Returns the total length now in the
 * buffer, guaranteed < bufSize.
 *
 * bodyLen is clamped to bufSize first. snprintf() returns the length it
 * *would* have written even when truncated to fit -- callers pass that
 * value straight through as bodyLen, so an over-length sentence used to
 * arrive here larger than the buffer itself. That produced buf+bodyLen
 * pointing past the end of buf, and bufSize-bodyLen underflowing as
 * size_t to ~4 billion, defeating the second snprintf's own bounds
 * check and writing the checksum suffix out of bounds. When truncation
 * did occur, snprintf still NUL-terminated at bufSize-1, so clamping
 * bodyLen there lines up exactly with where the real content ends.
 */
size_t appendChecksumAndTerminate(char* buf, size_t bodyLen, size_t bufSize) {
    if (bufSize == 0) {
        return 0;
    }
    if (bodyLen >= bufSize) {
        bodyLen = bufSize - 1;
    }

    const uint8_t checksum = (bodyLen > 0) ? nmeaChecksum(buf + 1) : 0;

    const int written = snprintf(buf + bodyLen, bufSize - bodyLen,
                                 "*%02X\r\n", checksum);
    if (written <= 0) {
        return bodyLen;
    }

    const size_t total = bodyLen + static_cast<size_t>(written);
    return (total < bufSize) ? total : (bufSize - 1);
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
 *
 * These are logging-only. Connection state is NOT mirrored into a bool
 * here (an earlier version did exactly that): NimBLE allows up to
 * CONFIG_BT_NIMBLE_MAX_CONNECTIONS (default 3) simultaneous centrals, so
 * a single bool toggled by every connect/disconnect goes wrong the
 * moment a second peer is briefly involved -- one peer disconnecting
 * would mark the whole device "disconnected" and silently stop
 * telemetry to a peer that is still connected and subscribed. Actual
 * connection state is queried fresh from the server each time via
 * getConnectedCount(), which is also simpler than keeping a duplicate
 * of state the library already tracks correctly.
 *
 * Advertising is also NOT manually restarted here: NimBLEServer already
 * does this itself on disconnect (m_advertiseOnDisconnect defaults
 * true, see NimBLEServer.cpp), so an explicit startAdvertising() call
 * here would just race a redundant call against the library's own.
 */
class BleTelemetryServerCallbacks : public NimBLEServerCallbacks {
public:
    void onConnect(NimBLEServer* server) override {
        (void)server;
        DBGLN("BLE telemetry: central connected");
    }

    void onDisconnect(NimBLEServer* server) override {
        (void)server;
        DBGLN("BLE telemetry: central disconnected");
    }
};

void BleTelemetry::begin() {
    NimBLEDevice::init(Config::BLE_DEVICE_NAME);

    server_ = NimBLEDevice::createServer();
    server_->setCallbacks(new BleTelemetryServerCallbacks());

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
    return (server_ != nullptr) && (server_->getConnectedCount() > 0);
}

void BleTelemetry::shutdown() {
    if (server_ == nullptr) {
        return;
    }

    NimBLEDevice::getAdvertising()->stop();
    NimBLEDevice::deinit(true);  // true: also releases the controller memory.
    server_ = nullptr;
    txCharacteristic_ = nullptr;

    DBGLN("BLE telemetry: shut down");
}

void BleTelemetry::update(const FlightData& data, const GPS::DateTime& utc,
                          bool baroValid, float pressurePa, float temperatureC) {
    if (!isConnected()) {
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
    sendBytes(buf, len);

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
    sendBytes(buf, len);
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
    sendBytes(buf, len);
}

void BleTelemetry::sendBytes(const char* data, size_t len) {
    if (server_ == nullptr || txCharacteristic_ == nullptr || len == 0) {
        return;
    }

    /*
     * Chunk size: 3 bytes below the smallest connected peer's negotiated
     * MTU, per the ATT_MTU / notification-payload relationship (3 bytes
     * of ATT opcode + handle overhead). Falls back to the default,
     * pre-negotiation MTU (23, i.e. a 20-byte payload) if no peer is
     * connected or a peer somehow reports 0 -- conservative rather than
     * risking a chunk NimBLE would truncate anyway.
     */
    constexpr uint16_t kDefaultMtu = 23;
    constexpr uint16_t kAttOverhead = 3;

    uint16_t chunkSize = kDefaultMtu - kAttOverhead;

    for (uint16_t connHandle : server_->getPeerDevices()) {
        const uint16_t mtu = server_->getPeerMTU(connHandle);
        if (mtu > kAttOverhead) {
            const uint16_t payload = mtu - kAttOverhead;
            if (payload < chunkSize) {
                chunkSize = payload;
            }
        }
    }

    if (chunkSize == 0) {
        return;
    }

    /*
     * Sent as a raw byte stream in MTU-sized pieces, not one notify()
     * per sentence. This is the correct model for a NUS-style
     * connection -- the far side reassembles by buffering until a
     * newline, exactly like a serial port -- rather than a workaround:
     * a single notify() truncates anything over one MTU instead of
     * splitting it (see the class-level comment on this method).
     */
    size_t offset = 0;
    while (offset < len) {
        const size_t remaining = len - offset;
        const size_t thisChunk = (remaining < chunkSize) ? remaining : chunkSize;

        txCharacteristic_->setValue(
            reinterpret_cast<const uint8_t*>(data + offset), thisChunk);
        txCharacteristic_->notify();

        offset += thisChunk;
    }
}

}  // namespace variometer
