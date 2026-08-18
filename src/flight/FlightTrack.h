#pragma once

#include <stdint.h>
#include <stddef.h>

#include "core/FlightData.h"

namespace variometer {

/*
 * Compact flight path store, in LZ-relative metres.
 *
 * Deliberately separate from FlightRecorder. FlightRecorder samples on a
 * fixed timer for the altitude trace and stores floats; a map wants
 * something different:
 *
 *   - positions already projected to metres, so the renderer never
 *     touches trigonometry while drawing
 *   - distance-based sampling, because a thermalling glider parked over
 *     one spot should not consume the buffer
 *   - a hard memory ceiling that degrades resolution rather than either
 *     growing without bound or discarding the start of the flight --
 *     losing the takeoff would defeat the whole map
 *
 * Storage is int16 metres, giving +/-32 km about the LZ at 1 m
 * resolution: finer than a 128-pixel screen can resolve at any sensible
 * zoom, and 8 bytes per point.
 */

struct TrackPoint {
    int16_t eastM;      // metres east of the LZ
    int16_t northM;     // metres north of the LZ
    int16_t altitudeM;  // metres, barometric
    uint16_t timeSec;   // seconds since takeoff
};

class FlightTrack {
public:
    FlightTrack() = default;

    /*
     * Begin a new flight, fixing the LZ as the projection origin.
     * The reference latitude's cosine is cached here because it is
     * constant for the flight and trigonometry is expensive without an
     * FPU.
     */
    void begin(float lzLatitude, float lzLongitude);

    void clear();

    /*
     * Offer a GPS sample. Returns true if it was stored.
     *
     * Rejects samples that are obviously untrustworthy rather than
     * letting them distort the map: no fix, too few satellites, or a
     * position jump too large to be real flight. A single wild fix would
     * otherwise stretch the auto-zoom to include it and shrink the
     * actual track to a dot.
     */
    bool addSample(const FlightData& data);

    /*
     * Live "you are here" position, valid even during a GPS dropout.
     *
     * Returns the last confirmed fix, extrapolated forward by straight-
     * line dead reckoning using the groundspeed/course recorded at that
     * fix. This is a display aid only -- dead-reckoned positions are
     * never written into the stored track, so a wrong guess cannot
     * corrupt flight history. When a real fix returns, the estimate is
     * simply discarded in favour of the truth.
     *
     * *isDeadReckoned is set true when the position is an estimate
     * rather than a fresh fix. Returns false if there has never been a
     * good fix (position not meaningful yet).
     *
     * Extrapolation is capped at Config::DEAD_RECKONING_TIMEOUT_MS: past
     * that, the velocity assumption is no longer trustworthy, so the
     * estimate freezes at its last position rather than compounding
     * error indefinitely.
     */
    bool getLiveEstimate(uint32_t nowMs, float* eastM, float* northM,
                         bool* isDeadReckoned) const;

    size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }
    const TrackPoint& at(size_t index) const { return points_[index]; }

    bool hasOrigin() const { return hasOrigin_; }

    // Bounding box of the stored track, in metres relative to the LZ.
    int16_t minEast() const { return minEast_; }
    int16_t maxEast() const { return maxEast_; }
    int16_t minNorth() const { return minNorth_; }
    int16_t maxNorth() const { return maxNorth_; }
    int16_t minAltitude() const { return minAlt_; }
    int16_t maxAltitude() const { return maxAlt_; }

    // Largest span of the bounding box in metres; drives zoom staging.
    int32_t extentMetres() const;

    // Seconds covered by the stored track.
    uint16_t durationSeconds() const;

    /*
     * Project a live position into the same local frame as the stored
     * points, so the renderer can plot the current aircraft position
     * without waiting for it to be recorded.
     */
    void project(float latitude, float longitude,
                 float* eastM, float* northM) const;

    // How many source samples were merged into each stored point after
    // decimation; exposed for diagnostics.
    uint16_t decimationFactor() const { return decimation_; }

private:
    void updateBounds(const TrackPoint& point);
    void recomputeBounds();

    // Refreshes the dead-reckoning reference from a fix that has already
    // passed the quality and plausibility checks in addSample().
    void updateDeadReckoning(const FlightData& data,
                             float eastM, float northM, uint32_t nowMs);

    /*
     * Halve the stored points when full, keeping every second one, and
     * double the distance threshold so the new resolution persists.
     *
     * This keeps the entire flight visible at progressively coarser
     * detail, which suits a map far better than a ring buffer: the
     * takeoff point and overall shape survive for the whole flight.
     */
    void decimate();

    static constexpr size_t MAX_POINTS = 512;

    TrackPoint points_[MAX_POINTS]{};
    size_t count_ = 0;

    float refLat_ = 0.0f;
    float refLon_ = 0.0f;
    float cosRefLat_ = 1.0f;
    bool hasOrigin_ = false;

    // Minimum movement before a new point is stored, in metres.
    float minDistanceM_ = 0.0f;
    uint16_t decimation_ = 1;

    // Last stored position, for the distance and plausibility checks.
    float lastEastM_ = 0.0f;
    float lastNorthM_ = 0.0f;
    uint32_t lastSampleMs_ = 0;
    bool hasLast_ = false;

    uint32_t startMs_ = 0;

    /*
     * Dead-reckoning reference: the most recent trustworthy fix and the
     * velocity implied by its groundspeed/course, kept separately from
     * lastEastM_/lastNorthM_ above because those track the last *stored*
     * point (which can lag behind by up to minDistanceM_ while the
     * aircraft is moving slowly), whereas dead reckoning needs the
     * freshest known position and velocity regardless of whether it was
     * far enough to store.
     */
    bool hasGoodFix_ = false;
    float lastGoodEastM_ = 0.0f;
    float lastGoodNorthM_ = 0.0f;
    uint32_t lastGoodTimeMs_ = 0;
    float velEastMs_ = 0.0f;
    float velNorthMs_ = 0.0f;

    int16_t minEast_ = 0;
    int16_t maxEast_ = 0;
    int16_t minNorth_ = 0;
    int16_t maxNorth_ = 0;
    int16_t minAlt_ = 0;
    int16_t maxAlt_ = 0;
};

}  // namespace variometer
