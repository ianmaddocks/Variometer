#include "utils/GeoUtils.h"

#include <math.h>

namespace variometer {
namespace geo {

float cosLatitude(float latitudeDegrees) {
    return cosf(latitudeDegrees * kDegToRad);
}

float distanceMetres(float lat1, float lon1, float lat2, float lon2) {
    const float lat1Rad = lat1 * kDegToRad;
    const float lat2Rad = lat2 * kDegToRad;

    const float dLat = (lat2 - lat1) * kDegToRad;
    const float dLon = (lon2 - lon1) * kDegToRad;

    const float sinHalfLat = sinf(dLat * 0.5f);
    const float sinHalfLon = sinf(dLon * 0.5f);

    const float a =
        (sinHalfLat * sinHalfLat) +
        (cosf(lat1Rad) * cosf(lat2Rad) * sinHalfLon * sinHalfLon);

    const float c = 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));

    return kEarthRadiusM * c;
}

float distanceKm(float lat1, float lon1, float lat2, float lon2) {
    return distanceMetres(lat1, lon1, lat2, lon2) / 1000.0f;
}

void toLocalMetres(float lat, float lon,
                   float refLat, float refLon, float cosRefLat,
                   float* eastM, float* northM) {
    /*
     * Equirectangular projection about the reference point.
     *
     * Longitude degrees shrink towards the poles, hence the cosRefLat
     * term; latitude degrees are very nearly constant in length. Error
     * is well under a metre across a typical flight, which is far finer
     * than a 128-pixel display can show.
     */
    if (eastM != nullptr) {
        *eastM = (lon - refLon) * kDegToRad * kEarthRadiusM * cosRefLat;
    }

    if (northM != nullptr) {
        *northM = (lat - refLat) * kDegToRad * kEarthRadiusM;
    }
}

}  // namespace geo
}  // namespace variometer
