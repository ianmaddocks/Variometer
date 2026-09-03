#include "sensors/GPS.h"

#include <Arduino.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "config/Config.h"
#include "sensors/MockGpsFeed.h"

namespace variometer {
namespace {

/*
 * A comma-delimited field within an NMEA sentence, as a pointer+length
 * window into the sentence buffer -- never a copy. Every NMEA sentence
 * arrives at 1-10Hz for the whole flight, so per-field/per-sentence
 * String allocation here (the previous implementation) was exactly the
 * heap-churn-in-a-hot-path pattern this codebase otherwise avoids (see
 * DPS310Sensor/VarioCalculator's own sample handshakes).
 */
struct FieldRef {
    const char* ptr = nullptr;
    size_t len = 0;
};

// Splits a NUL-terminated NMEA sentence on commas into up to maxFields
// FieldRefs (each pointing back into `sentence`, not allocated) and
// returns the total field count found -- which may exceed maxFields;
// fields beyond the cap are counted but not stored, matching the
// previous fieldIndex bookkeeping so the "enough fields present" checks
// below still see the true count.
int splitFields(const char* sentence, FieldRef* fields, int maxFields) {
    int fieldIndex = 0;
    const char* start = sentence;
    for (const char* p = sentence; ; ++p) {
        if (*p == ',' || *p == '\0') {
            if (fieldIndex < maxFields) {
                fields[fieldIndex].ptr = start;
                fields[fieldIndex].len = static_cast<size_t>(p - start);
            }
            ++fieldIndex;
            if (*p == '\0') {
                break;
            }
            start = p + 1;
        }
    }
    return fieldIndex;
}

bool fieldEquals(const FieldRef& f, const char* literal) {
    const size_t litLen = strlen(literal);
    return f.len == litLen && memcmp(f.ptr, literal, litLen) == 0;
}

char fieldChar(const FieldRef& f) {
    return (f.len > 0) ? f.ptr[0] : '\0';
}

// strtol/strtof read from f.ptr until the first non-numeric character,
// which -- since every field is immediately followed by ',' or the
// sentence's NUL -- always lands exactly on the field boundary. No
// bounded copy is needed for these; parseCoordinate()'s degrees/minutes
// split below is the one case that does need one.
long fieldToLong(const FieldRef& f) {
    if (f.len == 0) {
        return 0;
    }
    return strtol(f.ptr, nullptr, 10);
}

float fieldToFloat(const FieldRef& f) {
    if (f.len == 0) {
        return 0.0f;
    }
    return strtof(f.ptr, nullptr);
}

float parseCoordinate(const char* value, size_t valueLen, char hemi) {
    if (valueLen == 0) {
        return 0.0f;
    }

    const char* dotPtr = static_cast<const char*>(memchr(value, '.', valueLen));
    if (dotPtr == nullptr) {
        return 0.0f;
    }
    const int dot = static_cast<int>(dotPtr - value);
    if (dot <= 2) {
        return 0.0f;
    }

    /*
     * Degrees are the fixed-width prefix before the last two whole-
     * number digits of minutes (DDMM.MMMM / DDDMM.MMMM) -- unlike every
     * other numeric field here, there is no delimiter between the
     * degrees and minutes digits for strtol to stop at on its own, so
     * this one field needs a small bounded stack copy.
     */
    char degBuf[8];
    const size_t degLen = static_cast<size_t>(dot - 2);
    if (degLen >= sizeof(degBuf)) {
        return 0.0f;
    }
    memcpy(degBuf, value, degLen);
    degBuf[degLen] = '\0';
    const int degrees = atoi(degBuf);

    const float minutes = strtof(value + (dot - 2), nullptr);
    if (degrees < 0 || isnan(minutes)) {
        return 0.0f;
    }

    float result = static_cast<float>(degrees) + (minutes / 60.0f);
    if (hemi == 'S' || hemi == 'W') {
        result = -result;
    }
    return result;
}

}  // namespace

void GPS::begin() {
    /*
     * Must be called before begin() -- the driver allocates the RX
     * buffer during begin() and ignores later resize requests.
     *
     * The 256-byte default holds only ~22 ms of traffic at 115200 baud,
     * so any pause in the main loop (a display refresh, for instance)
     * silently truncated NMEA sentences mid-line.
     */
    Serial1.setRxBufferSize(Config::GPS_RX_BUFFER_BYTES);
    Serial1.begin(Config::GPS_BAUD, SERIAL_8N1, Config::GPS_RX, Config::GPS_TX);
    hasData_ = false;
    latitude_ = 0.0f;
    longitude_ = 0.0f;
    altitude_ = 0.0f;
    groundSpeed_ = 0.0f;
    track_ = 0.0f;
    satellites_ = 0;
    fix_ = false;
    altitudeSampleSeq_ = 0;
    altitudeSampleTimeMs_ = 0;
    positionSampleSeq_ = 0;
    utcDateTime_ = DateTime{};
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
    DBGLN("Mock GPS feed enabled");
}

bool GPS::isMockEnabled() const { return mockEnabled_; }

void GPS::feedMockSentence(const char* sentence) {
    feedSerialSentence(sentence);
}

void GPS::feedSerialSentence(const char* sentence) {
    const bool isGga = strncmp(sentence, "$GPGGA", 6) == 0 || strncmp(sentence, "$GNGGA", 6) == 0;
    const bool isRmc = !isGga &&
        (strncmp(sentence, "$GPRMC", 6) == 0 || strncmp(sentence, "$GNRMC", 6) == 0);

    if (isGga) {
        FieldRef fields[15];
        const int fieldIndex = splitFields(sentence, fields, 15);

        if (fieldIndex > 9) {
            const int quality = static_cast<int>(fieldToLong(fields[6]));
            const int sats = static_cast<int>(fieldToLong(fields[7]));
            if (sats >= 0) {
                satellites_ = static_cast<uint8_t>(sats);
            }
            fix_ = quality > 0;

            /*
             * Only treat the altitude field as a sample when the
             * receiver actually has a fix AND populated the field.
             *
             * With no fix the field is empty, and toFloat() turns that
             * into a perfectly plausible-looking 0.0 m that clears the
             * range check below -- so the vario was being fed a
             * rock-steady 0 m at the GGA rate and reported a confident
             * 0.00 m/s rather than no data at all.
             */
            if (fix_ && fields[9].len > 0) {
                const float altitude = fieldToFloat(fields[9]);
                if (!isnan(altitude) && altitude > -1000.0f) {
                    altitude_ = altitude;

                    // A GGA sentence is the sole source of GPS altitude, so
                    // this is exactly "a new altitude sample arrived".
                    altitudeSampleTimeMs_ = millis();
                    ++altitudeSampleSeq_;
                }
            }
            hasData_ = true;
        }
    } else if (isRmc) {
        FieldRef fields[13];
        const int fieldIndex = splitFields(sentence, fields, 13);

        if (fieldIndex > 7 && fieldEquals(fields[2], "A")) {
            if (fields[1].len >= 6 && fields[9].len >= 6) {
                const int timeValue = static_cast<int>(fieldToLong(fields[1]));
                const int dateValue = static_cast<int>(fieldToLong(fields[9]));
                const uint8_t day = static_cast<uint8_t>(dateValue / 10000);
                const uint8_t month = static_cast<uint8_t>((dateValue / 100) % 100);
                const uint8_t year = static_cast<uint8_t>(dateValue % 100);
                if (day >= 1 && day <= 31 && month >= 1 && month <= 12) {
                    utcDateTime_.hour = static_cast<uint8_t>(timeValue / 10000);
                    utcDateTime_.minute = static_cast<uint8_t>((timeValue / 100) % 100);
                    utcDateTime_.second = static_cast<uint8_t>(timeValue % 100);
                    utcDateTime_.day = day;
                    utcDateTime_.month = month;
                    utcDateTime_.year = static_cast<uint16_t>(2000 + year);
                    utcDateTime_.valid = utcDateTime_.hour < 24 && utcDateTime_.minute < 60 &&
                                         utcDateTime_.second < 60;
                }
            }
            const float lat = parseCoordinate(fields[3].ptr, fields[3].len, fieldChar(fields[4]));
            const float lon = parseCoordinate(fields[5].ptr, fields[5].len, fieldChar(fields[6]));
            const float speed = fieldToFloat(fields[7]) * 0.514444f;
            const float course = fieldToFloat(fields[8]);
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

            // A valid RMC sentence is the sole source of track/groundSpeed,
            // so this is exactly "a new position/velocity sample arrived".
            ++positionSampleSeq_;
        }
    }
}

void GPS::update() {
    if (mockEnabled_) {
        if (mockSentenceIndex_ < mockgps::kSentenceFeedCount) {
            const uint32_t now = millis();
            if (mockStartMs_ == 0 || (now - mockStartMs_) >= 1000) {
                feedMockSentence(mockgps::kSentenceFeed[mockSentenceIndex_++]);
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
                feedSerialSentence(buffer);
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
uint32_t GPS::getAltitudeSampleSequence() const { return altitudeSampleSeq_; }
uint32_t GPS::getAltitudeSampleTimeMs() const { return altitudeSampleTimeMs_; }
float GPS::getGroundSpeed() const { return groundSpeed_; }
float GPS::getTrack() const { return track_; }
uint32_t GPS::getPositionSampleSequence() const { return positionSampleSeq_; }
uint8_t GPS::getSatellites() const { return satellites_; }
bool GPS::getFixStatus() const { return fix_; }
GPS::DateTime GPS::getUtcDateTime() const { return utcDateTime_; }

}  // namespace variometer
