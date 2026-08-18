#pragma once

#include <stdint.h>

namespace variometer {

class DisplayManager;

namespace display_helpers {

/*
 * LZ/takeoff marker: a circle with a cross through it.
 *
 * Shared between FlightMapScreen (marking the LZ on the plan/3D views)
 * and WindDirectionScreen (marking the direction to the LZ on the
 * compass ring), so the pilot learns one symbol for "this is the LZ"
 * rather than two different marks that happen to mean the same thing.
 */
void drawLzMarker(DisplayManager& display, int16_t x, int16_t y);

}  // namespace display_helpers
}  // namespace variometer
