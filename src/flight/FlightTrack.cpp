#include "flight/FlightTrack.h"

#include <Arduino.h>
#include <math.h>

#include "config/Config.h"
#include "utils/GeoUtils.h"

namespace variometer {
namespace {

/*
 * Fastest ground speed treated as physically possible, in m/s.
 *
 * Used to reject position jumps. A paramotor tops out well below this;
 * the limit exists to catch corrupt fixes, not to constrain flying.
 */
constexpr float kMaxPlausibleSpeedMs = 80.0f;

// Smallest movement stored at full resolution. Below this the aircraft
// is effectively stationary and extra points add nothing to the map.
constexpr float kInitialMinDistanceM = 5.0f;

// Clamp helper for the int16 storage range (+/-32 km about the LZ).
int16_t clampToInt16(float metres) {
    if (metres > 32767.0f) {
        return 32767;
    }
    if (metres < -32768.0f) {
        return -32768;
    }
    return static_cast<int16_t>(metres);
}

}  // namespace

void FlightTrack::begin(float lzLatitude, float lzLongitude) {
    clear();

    refLat_ = lzLatitude;
    refLon_ = lzLongitude;
    cosRefLat_ = geo::cosLatitude(lzLatitude);
    hasOrigin_ = true;
    startMs_ = millis();

    DBGF("FlightTrack: origin set to %.5f, %.5f\n", refLat_, refLon_);
}

void FlightTrack::clear() {
    count_ = 0;
    minDistanceM_ = kInitialMinDistanceM;
    decimation_ = 1;
    hasLast_ = false;
    lastEastM_ = 0.0f;
    lastNorthM_ = 0.0f;
    lastSampleMs_ = 0;
    hasOrigin_ = false;

    hasGoodFix_ = false;
    lastGoodEastM_ = 0.0f;
    lastGoodNorthM_ = 0.0f;
    lastGoodTimeMs_ = 0;
    velEastMs_ = 0.0f;
    velNorthMs_ = 0.0f;

    minEast_ = 0;
    maxEast_ = 0;
    minNorth_ = 0;
    maxNorth_ = 0;
    minAlt_ = 0;
    maxAlt_ = 0;
}

void FlightTrack::project(float latitude, float longitude,
                          float* eastM, float* northM) const {
    if (!hasOrigin_) {
        if (eastM != nullptr) *eastM = 0.0f;
        if (northM != nullptr) *northM = 0.0f;
        return;
    }

    geo::toLocalMetres(latitude, longitude,
                       refLat_, refLon_, cosRefLat_,
                       eastM, northM);
}

bool FlightTrack::addSample(const FlightData& data) {
    if (!hasOrigin_) {
        return false;
    }

    // Quality gate: an unfixed or weak solution is not worth plotting.
    if (!data.gpsFix || data.satellites < 4) {
        return false;
    }

    if (!isfinite(data.latitude) || !isfinite(data.longitude)) {
        return false;
    }

    // A zeroed position is the classic "no fix yet" placeholder and must
    // never be plotted -- it would drag the map to the Gulf of Guinea.
    if (data.latitude == 0.0f && data.longitude == 0.0f) {
        return false;
    }

    float eastM = 0.0f;
    float northM = 0.0f;
    project(data.latitude, data.longitude, &eastM, &northM);

    const uint32_t now = millis();
    bool farEnoughToStore = true;

    if (hasLast_) {
        const float deltaEast = eastM - lastEastM_;
        const float deltaNorth = northM - lastNorthM_;
        const float distance = sqrtf((deltaEast * deltaEast) +
                                     (deltaNorth * deltaNorth));

        const float elapsedSec =
            static_cast<float>(now - lastSampleMs_) / 1000.0f;

        /*
         * Reject implausible jumps. Checked as a speed rather than a
         * fixed distance so a legitimate gap in sampling (a slow loop,
         * a lost fix) is not mistaken for a bad fix.
         *
         * Rejected outright, before touching dead reckoning: a jump this
         * size is exactly the kind of corrupt fix that would send the
         * DR velocity estimate wild too.
         */
        if (elapsedSec > 0.0f &&
            (distance / elapsedSec) > kMaxPlausibleSpeedMs) {
            DBGF("FlightTrack: rejected %.0fm jump in %.1fs\n",
                 distance, elapsedSec);
            return false;
        }

        // Not moved far enough to be worth a new stored point -- but
        // still a trustworthy fix, so dead reckoning is refreshed below.
        farEnoughToStore = (distance >= minDistanceM_);
    }

    /*
     * Refresh the dead-reckoning reference from this fix regardless of
     * whether it becomes a stored point. DR needs the freshest known
     * position and velocity, not the last stored waypoint, which can lag
     * behind by up to minDistanceM_ while the aircraft moves slowly.
     */
    updateDeadReckoning(data, eastM, northM, now);

    if (!farEnoughToStore) {
        return false;
    }

    if (count_ >= MAX_POINTS) {
        decimate();
    }

    TrackPoint point;
    point.eastM = clampToInt16(eastM);
    point.northM = clampToInt16(northM);
    point.altitudeM = clampToInt16(data.barometricAltitude);
    point.timeSec = static_cast<uint16_t>((now - startMs_) / 1000U);

    const bool first = (count_ == 0);
    points_[count_++] = point;

    if (first) {
        minEast_ = maxEast_ = point.eastM;
        minNorth_ = maxNorth_ = point.northM;
        minAlt_ = maxAlt_ = point.altitudeM;
    } else {
        updateBounds(point);
    }

    lastEastM_ = eastM;
    lastNorthM_ = northM;
    lastSampleMs_ = now;
    hasLast_ = true;

    return true;
}

void FlightTrack::updateDeadReckoning(const FlightData& data,
                                      float eastM, float northM,
                                      uint32_t nowMs) {
    /*
     * Velocity from the GPS's own groundspeed/course rather than by
     * differencing this fix against the previous one. Differencing two
     * nearby positions amplifies their individual noise into a much
     * noisier velocity; the receiver's own speed/course solution is
     * filtered internally and is a materially better estimate.
     */
    const float speedMs = data.groundSpeed / 3.6f;  // km/h -> m/s
    const float trackRad = data.track * geo::kDegToRad;

    lastGoodEastM_ = eastM;
    lastGoodNorthM_ = northM;
    lastGoodTimeMs_ = nowMs;
    velEastMs_ = speedMs * sinf(trackRad);
    velNorthMs_ = speedMs * cosf(trackRad);
    hasGoodFix_ = true;
}

bool FlightTrack::getLiveEstimate(uint32_t nowMs, float* eastM, float* northM,
                                  bool* isDeadReckoned) const {
    if (!hasGoodFix_) {
        if (isDeadReckoned != nullptr) *isDeadReckoned = false;
        return false;
    }

    const uint32_t elapsedMs = nowMs - lastGoodTimeMs_;

    if (elapsedMs > Config::DEAD_RECKONING_TIMEOUT_MS) {
        // Extrapolation has run too long to be trusted further: hold
        // position rather than let an early velocity estimate carry the
        // marker arbitrarily far from the truth.
        if (eastM != nullptr) *eastM = lastGoodEastM_;
        if (northM != nullptr) *northM = lastGoodNorthM_;
        if (isDeadReckoned != nullptr) *isDeadReckoned = true;
        return true;
    }

    const float elapsedSec = static_cast<float>(elapsedMs) / 1000.0f;

    if (eastM != nullptr) *eastM = lastGoodEastM_ + (velEastMs_ * elapsedSec);
    if (northM != nullptr) *northM = lastGoodNorthM_ + (velNorthMs_ * elapsedSec);

    if (isDeadReckoned != nullptr) {
        *isDeadReckoned = (elapsedMs > Config::DEAD_RECKONING_LIVE_GRACE_MS);
    }

    return true;
}

void FlightTrack::updateBounds(const TrackPoint& point) {
    if (point.eastM < minEast_) minEast_ = point.eastM;
    if (point.eastM > maxEast_) maxEast_ = point.eastM;
    if (point.northM < minNorth_) minNorth_ = point.northM;
    if (point.northM > maxNorth_) maxNorth_ = point.northM;
    if (point.altitudeM < minAlt_) minAlt_ = point.altitudeM;
    if (point.altitudeM > maxAlt_) maxAlt_ = point.altitudeM;
}

void FlightTrack::recomputeBounds() {
    if (count_ == 0) {
        minEast_ = maxEast_ = 0;
        minNorth_ = maxNorth_ = 0;
        minAlt_ = maxAlt_ = 0;
        return;
    }

    minEast_ = maxEast_ = points_[0].eastM;
    minNorth_ = maxNorth_ = points_[0].northM;
    minAlt_ = maxAlt_ = points_[0].altitudeM;

    for (size_t i = 1; i < count_; ++i) {
        updateBounds(points_[i]);
    }
}

void FlightTrack::decimate() {
    /*
     * Keep every second point, always retaining index 0 so the takeoff
     * marker never moves, and always retaining the most recent point so
     * the head of the track stays attached to the aircraft.
     */
    size_t out = 0;

    for (size_t in = 0; in < count_; in += 2) {
        points_[out++] = points_[in];
    }

    // If the newest point fell on an odd index it was just dropped;
    // put it back so the line still reaches the current position.
    if (((count_ - 1) % 2) != 0 && out < MAX_POINTS) {
        points_[out++] = points_[count_ - 1];
    }

    count_ = out;
    minDistanceM_ *= 2.0f;
    decimation_ = static_cast<uint16_t>(decimation_ * 2U);

    recomputeBounds();

    DBGF("FlightTrack: decimated to %u points, min spacing now %.0fm\n",
         static_cast<unsigned>(count_), minDistanceM_);
}

int32_t FlightTrack::extentMetres() const {
    if (count_ == 0) {
        return 0;
    }

    const int32_t spanEast =
        static_cast<int32_t>(maxEast_) - static_cast<int32_t>(minEast_);
    const int32_t spanNorth =
        static_cast<int32_t>(maxNorth_) - static_cast<int32_t>(minNorth_);

    return (spanEast > spanNorth) ? spanEast : spanNorth;
}

uint16_t FlightTrack::durationSeconds() const {
    if (count_ == 0) {
        return 0;
    }

    return points_[count_ - 1].timeSec;
}

}  // namespace variometer
