#include "flight/FlightRecorder.h"

namespace variometer {

void FlightRecorder::clear() { history_.clear(); }

void FlightRecorder::addPoint(float altitude, float timeSeconds, float latitude, float longitude) {
    TracePoint point;
    point.altitude = altitude;
    point.timeSeconds = timeSeconds;
    point.latitude = latitude;
    point.longitude = longitude;
    history_.push(point);
}

size_t FlightRecorder::size() const { return history_.size(); }
const TracePoint& FlightRecorder::at(size_t index) const { return history_.at(index); }

}  // namespace variometer
