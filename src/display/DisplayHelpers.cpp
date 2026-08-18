#include "display/DisplayHelpers.h"

#include "display/DisplayManager.h"

namespace variometer {
namespace display_helpers {

void drawLzMarker(DisplayManager& display, int16_t x, int16_t y) {
    // Circle with a cross through it: distinct from the aircraft arrow
    // at a glance, and still legible when a track line passes over it.
    display.display().drawCircle(x, y, 3, SH110X_WHITE);
    display.display().drawLine(x - 5, y, x + 5, y, SH110X_WHITE);
    display.display().drawLine(x, y - 5, x, y + 5, SH110X_WHITE);
}

}  // namespace display_helpers
}  // namespace variometer
