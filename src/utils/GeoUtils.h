#pragma once

#include <stdint.h>

namespace variometer {
namespace geo {

/*
 * Geodesic helpers.
 *
 * Latitude/longitude are never treated as Cartesian coordinates. Two
 * different projections are used depending on what the caller needs:
 *
 *   distanceMetres()  -- haversine, accurate over any distance, used for
 *                        the reported distance-to-LZ.
 *   toLocalMetres()   -- equirectangular, valid over the tens of
 *                        kilometres a flight covers and far cheaper.
 *                        The ESP32-C3 has no FPU, so trig is expensive
 *                        and the map redraw cannot afford a haversine
 *                        per point.
 *
 * The local projection places the LZ at the origin with +east and
 * +north in metres, which is what the map renderer plots in.
 */

constexpr float kEarthRadiusM = 6371000.0f;
constexpr float kDegToRad = 0.017453292519943295f;

// Great-circle distance in metres.
float distanceMetres(float lat1, float lon1, float lat2, float lon2);

// Same, in kilometres -- convenience for display code.
float distanceKm(float lat1, float lon1, float lat2, float lon2);

/*
 * Project a position into metres east/north of a reference point.
 *
 * cosRefLat is passed in rather than recomputed because the reference
 * (the LZ) is fixed for a whole flight; computing cosf() once per flight
 * instead of once per track point matters on a soft-float target.
 */
void toLocalMetres(float lat, float lon,
                   float refLat, float refLon, float cosRefLat,
                   float* eastM, float* northM);

// cosf(latitude in degrees), for caching alongside a reference point.
float cosLatitude(float latitudeDegrees);

}  // namespace geo
}  // namespace variometer
