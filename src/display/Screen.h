#pragma once

#include "core/FlightData.h"

namespace variometer {

class DisplayManager;

enum class ScreenId : uint8_t {
    StartUp,
    PreTakeoff,
    Variometer,
    VarioBar,
    AltitudeTrace,
    WindDirection,
    FlightMap,
    Settings,
    PowerOff,
    Landed
};

class Screen {
public:
    virtual ~Screen() = default;
    virtual void enter() = 0;
    virtual void update(const FlightData& data) = 0;
    virtual void draw(DisplayManager& display, const FlightData& data) = 0;
    virtual void exit() = 0;
};

}  // namespace variometer
