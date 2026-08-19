#pragma once

#include <stdint.h>

#include "core/FlightData.h"
#include "sensors/GPS.h"

class NimBLEServer;
class NimBLECharacteristic;
class NimBLEServerCallbacks;

namespace variometer {

/*
 * Streams live flight data to a connected phone app over BLE, as a
 * Nordic UART Service (NUS) GATT server sending plain-text NMEA.
 *
 * See Config.h (BLE_* constants) for the important caveat: this targets
 * the general "BLE vario accessory" convention (NUS + GGA/RMC/LK8EX1),
 * not a confirmed FlyGaggle specification. If FlyGaggle does not
 * recognise the device, that is the first thing to check against its
 * own pairing documentation.
 *
 * Design choices:
 *   - Sentences are only sent while something is actually connected and
 *     subscribed to notifications -- building and transmitting NMEA to
 *     nobody would be pure waste on a battery-powered device.
 *   - GPS position (GGA/RMC) and the vario reading (LK8EX1) are sent on
 *     independent timers, because a client app wants position updates
 *     at ordinary GPS rates but a vario needle/tone reads as laggy below
 *     a few Hz -- see Config::BLE_VARIO_SENTENCE_INTERVAL_MS.
 *   - The RX characteristic (phone -> device) required by the NUS
 *     profile is created but never read; nothing in this device accepts
 *     remote input. It exists purely so BLE central apps that assume a
 *     complete NUS profile do not refuse the connection.
 */
class BleTelemetry {
public:
    BleTelemetry() = default;

    void begin();

    // Safe to call every loop pass; internally rate-limited and a no-op
    // whenever nothing is connected. utc is used to fill GGA/RMC's
    // time/date fields; sentences are still sent with a zeroed time if
    // utc.valid is false, since withholding position over that would be
    // worse than an imprecise timestamp.
    void update(const FlightData& data, const GPS::DateTime& utc,
               bool baroValid, float pressurePa, float temperatureC);

    bool isConnected() const;

private:
    void sendGpsSentences(const FlightData& data, const GPS::DateTime& utc);
    void sendVarioSentence(const FlightData& data, bool baroValid,
                           float pressurePa, float temperatureC);

    /*
     * Sends data as one or more BLE notifications, each no larger than
     * the smallest connected peer's negotiated ATT payload (MTU - 3).
     *
     * A single notify() does NOT do this: NimBLECharacteristic::notify()
     * silently truncates anything longer than the MTU rather than
     * splitting it, and the default MTU before negotiation is only 23
     * bytes (20-byte payload) -- well under a ~74-byte GGA sentence.
     * Some centrals (notably Android, unless the app explicitly calls
     * requestMtu()) never negotiate up from that default. Splitting here
     * is the correct fix rather than a workaround: a NUS-style
     * connection is meant to be read as a continuous byte stream by the
     * far side, reassembled on newlines, not one sentence per
     * notification.
     */
    void sendBytes(const char* data, size_t len);

    NimBLEServer* server_ = nullptr;
    NimBLECharacteristic* txCharacteristic_ = nullptr;

    uint32_t lastGpsSentMs_ = 0;
    uint32_t lastVarioSentMs_ = 0;
};

}  // namespace variometer
