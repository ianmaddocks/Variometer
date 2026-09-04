#pragma once

#include "display/Screen.h"

namespace variometer {

class WifiQrScreen : public Screen {
public:
    void enter() override;
    void update(const FlightData& data) override;
    void draw(DisplayManager& display, const FlightData& data) override;
    void exit() override;
    bool needsRedraw() const override { return !rendered_; }

private:
    // Set once draw() has run for the current visit to this screen and
    // cleared again on enter() -- see needsRedraw().
    bool rendered_ = false;

    // Cached from the most recent draw() call, purely so exit() can put
    // the panel's row-scan rate back to normal (see the .cpp). Screen's
    // exit() takes no DisplayManager& -- every other screen has no need
    // of one there -- so this is how this one screen reaches the display
    // without changing that interface for all of them. DisplayManager
    // outlives every screen, so a pointer from an earlier visit is always
    // still valid even if this is used before draw() runs again.
    DisplayManager* lastDisplay_ = nullptr;
};

}  // namespace variometer