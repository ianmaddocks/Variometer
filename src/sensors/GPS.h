#pragma once

#include <Arduino.h>

namespace variometer {

class GPS {
public:
    struct DateTime {
        uint16_t year = 0;
        uint8_t month = 0;
        uint8_t day = 0;
        uint8_t hour = 0;
        uint8_t minute = 0;
        uint8_t second = 0;
        bool valid = false;
    };

    GPS() = default;
    void begin();
    void update();
    void enableMockFeed();
    bool isMockEnabled() const;
    bool hasData() const;
    float getLatitude() const;
    float getLongitude() const;
    float getAltitude() const;

    /*
     * Altitude sample handshake, mirroring MS5611Sensor's. Needed
     * because GPS altitude can be selected as the vario's altitude
     * source (see Config::ALTITUDE_SOURCE_GPS), and the vario must be
     * able to tell a genuinely new GGA fix from a repeat of the last one
     * -- otherwise it re-derives the same duplicate-sample staircase
     * problem that the barometer path already had to solve.
     */
    uint32_t getAltitudeSampleSequence() const;
    uint32_t getAltitudeSampleTimeMs() const;

    float getGroundSpeed() const;
    float getTrack() const;
    uint8_t getSatellites() const;
    bool getFixStatus() const;
    DateTime getUtcDateTime() const;

private:
    void feedMockSentence(const String& sentence);
    void feedSerialSentence(const String& sentence);

    bool hasData_ = false;
    float latitude_ = 0.0f;
    float longitude_ = 0.0f;
    float altitude_ = 0.0f;
    float groundSpeed_ = 0.0f;
    float track_ = 0.0f;
    uint8_t satellites_ = 0;
    bool fix_ = false;
    uint32_t altitudeSampleSeq_ = 0;
    uint32_t altitudeSampleTimeMs_ = 0;
    DateTime utcDateTime_;
    bool mockEnabled_ = false;
    uint32_t mockStartMs_ = 0;
    uint32_t mockSentenceIndex_ = 0;
    float mockBaseLatitude_ = 51.5074f;
    float mockBaseLongitude_ = -0.1278f;
    float mockBaseAltitude_ = 100.0f;
};

}  // namespace variometer
