#pragma once

#include <Arduino.h>

namespace variometer {

class GPS {
public:
    GPS() = default;
    void begin();
    void update();
    void enableMockFeed();
    bool isMockEnabled() const;
    bool hasData() const;
    float getLatitude() const;
    float getLongitude() const;
    float getAltitude() const;
    float getGroundSpeed() const;
    float getTrack() const;
    uint8_t getSatellites() const;
    bool getFixStatus() const;

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
    bool mockEnabled_ = false;
    uint32_t mockStartMs_ = 0;
    uint32_t mockSentenceIndex_ = 0;
    float mockBaseLatitude_ = 51.5074f;
    float mockBaseLongitude_ = -0.1278f;
    float mockBaseAltitude_ = 100.0f;
};

}  // namespace variometer
