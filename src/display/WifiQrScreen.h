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
};

}  // namespace variometer