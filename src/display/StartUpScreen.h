#pragma once

#include "display/Screen.h"

namespace variometer {

class StartUpScreen : public Screen {
public:
    void enter() override;
    void update(const FlightData& data) override;
    void draw(DisplayManager& display, const FlightData& data) override;
    void exit() override;
};

}  // namespace variometer
