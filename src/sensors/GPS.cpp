#include "sensors/GPS.h"

#include <Arduino.h>
#include <math.h>

#include "config/Config.h"
#include "sensors/MockGpsFeed.h"

namespace variometer {
namespace {

float parseCoordinate(const String& value, const String& hemi) {
    if (value.length() == 0) {
        return 0.0f;
    }

    const int dot = value.indexOf('.');
    if (dot <= 2) {
        return 0.0f;
    }

    const int degrees = value.substring(0, dot - 2).toInt();
    const float minutes = value.substring(dot - 2).toFloat();
    if (degrees < 0 || isnan(minutes)) {
        return 0.0f;
    }

    float result = static_cast<float>(degrees) + (minutes / 60.0f);
    if (hemi == "S" || hemi == "W") {
        result = -result;
    }
    return result;
}

}  // namespace

void GPS::begin() {
    Serial1.begin(Config::GPS_BAUD, SERIAL_8N1, Config::GPS_RX, Config::GPS_TX);
    hasData_ = false;
    latitude_ = 0.0f;
    longitude_ = 0.0f;
    altitude_ = 0.0f;
    groundSpeed_ = 0.0f;
    track_ = 0.0f;
    satellites_ = 0;
    fix_ = false;
    mockEnabled_ = false;
    mockStartMs_ = 0;
}

void GPS::enableMockFeed() {
    mockEnabled_ = true;
    mockStartMs_ = millis();
    mockSentenceIndex_ = 0;
    hasData_ = false;
    satellites_ = 0;
    fix_ = false;
    latitude_ = mockBaseLatitude_;
    longitude_ = mockBaseLongitude_;
    altitude_ = mockBaseAltitude_;
    groundSpeed_ = 0.0f;
    track_ = 0.0f;
    Serial.println("Mock GPS feed enabled");
}

bool GPS::isMockEnabled() const { return mockEnabled_; }

void GPS::feedMockSentence(const String& sentence) {
    feedSerialSentence(sentence);
}

void GPS::feedSerialSentence(const String& sentence) {
    if (sentence.startsWith("$GPGGA")) {
        String fields[15];
        for (int i = 0; i < 15; ++i) {
            fields[i] = "";
        }
        int fieldIndex = 0;
        int start = 0;
        for (int i = 0; i <= sentence.length(); ++i) {
            if (sentence[i] == ',' || sentence[i] == '\0') {
                if (fieldIndex < 15) {
                    fields[fieldIndex] = sentence.substring(start, i);
                }
                fieldIndex++;
                start = i + 1;
            }
        }

        if (fieldIndex > 9) {
            const int quality = fields[6].toInt();
            const int sats = fields[7].toInt();
            if (sats >= 0) {
                satellites_ = static_cast<uint8_t>(sats);
            }
            fix_ = quality > 0;
            const float altitude = fields[9].toFloat();
            if (!isnan(altitude) && altitude > -1000.0f) {
                altitude_ = altitude;
            }
            hasData_ = true;
        }
    } else if (sentence.startsWith("$GPRMC")) {
        String fields[13];
        for (int i = 0; i < 13; ++i) {
            fields[i] = "";
        }
        int fieldIndex = 0;
        int start = 0;
        for (int i = 0; i <= sentence.length(); ++i) {
            if (sentence[i] == ',' || sentence[i] == '\0') {
                if (fieldIndex < 13) {
                    fields[fieldIndex] = sentence.substring(start, i);
                }
                fieldIndex++;
                start = i + 1;
            }
        }

        if (fieldIndex > 7 && fields[2] == "A") {
            const float lat = parseCoordinate(fields[3], fields[4]);
            const float lon = parseCoordinate(fields[5], fields[6]);
            const float speed = fields[7].toFloat() * 0.514444f;
            const float course = fields[8].toFloat();
            if (!isnan(lat) && !isnan(lon) && fabsf(lat) <= 90.0f && fabsf(lon) <= 180.0f) {
                latitude_ = lat;
                longitude_ = lon;
            }
            if (!isnan(speed) && speed >= 0.0f) {
                groundSpeed_ = speed;
            }
            if (!isnan(course) && course >= 0.0f && course <= 360.0f) {
                track_ = course;
            }
            fix_ = true;
            hasData_ = true;
        }
    }
}

void GPS::update() {
    if (mockEnabled_) {
        if (mockSentenceIndex_ < mockgps::kSentenceFeedCount) {
            const uint32_t now = millis();
            if (mockStartMs_ == 0 || (now - mockStartMs_) >= 1000) {
                feedMockSentence(String(mockgps::kSentenceFeed[mockSentenceIndex_++]));
                mockStartMs_ = now;
            }
        }
        return;
    }

    static char buffer[220];
    static size_t index = 0;

    while (Serial1.available()) {
        const char c = static_cast<char>(Serial1.read());
        if (c == '\n') {
            buffer[index] = '\0';
            if (index > 0) {
                const String sentence(buffer);
                //Serial.println(sentence);
                feedSerialSentence(sentence);
            }
            index = 0;
        } else if (index < sizeof(buffer) - 1) {
            buffer[index++] = c;
        }
    }
}

bool GPS::hasData() const { return hasData_; }
float GPS::getLatitude() const { return latitude_; }
float GPS::getLongitude() const { return longitude_; }
float GPS::getAltitude() const { return altitude_; }
float GPS::getGroundSpeed() const { return groundSpeed_; }
float GPS::getTrack() const { return track_; }
uint8_t GPS::getSatellites() const { return satellites_; }
bool GPS::getFixStatus() const { return fix_; }

}  // namespace variometer
