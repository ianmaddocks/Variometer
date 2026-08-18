#pragma once

#include "display/Screen.h"

namespace variometer {

class WindDirectionScreen : public Screen {
public:
    void enter() override;
    void update(const FlightData& data) override;
    void draw(DisplayManager& display, const FlightData& data) override;
    void exit() override;

private:
    /*
     * Filled dart pointing along screenBearingDeg (0=up, clockwise),
     * with the speed written inside the fill. Sized by confidence.
     *
     * screenBearingDeg must already be track-relative -- this screen is
     * heading-up, not north-up, so "up" means direction of travel, not
     * geographic north. See relativeBearing() in the .cpp.
     */
    void drawWindArrow(DisplayManager& display, float screenBearingDeg,
                       float speedMs, float confidence) const;

    // N/S/E/W ring, rotated so "up" tracks the current heading.
    void drawCompassRing(DisplayManager& display, float trackDeg) const;

    // Twin triangles meeting at zero: climb fills upward, sink downward.
    void drawVarioGauge(DisplayManager& display, float verticalSpeed) const;
};

}  // namespace variometer
