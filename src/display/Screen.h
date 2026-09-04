#pragma once

#include "core/FlightData.h"

namespace variometer {

class DisplayManager;

enum class ScreenId : uint8_t {
    VarioBar,
    AltitudeTrace,
    WindDirection,
    FlightMap,
    Settings,
    WifiQr,
    Landed
};

class Screen {
public:
    virtual ~Screen() = default;
    virtual void enter() = 0;
    virtual void update(const FlightData& data) = 0;
    virtual void draw(DisplayManager& display, const FlightData& data) = 0;
    virtual void exit() = 0;

    /*
     * True if the OLED should be cleared/redrawn/pushed this cycle for
     * this screen. Defaults to always-redraw, which is what every
     * flight-data-driven screen needs. A screen whose content is fixed
     * once drawn -- nothing to observe, so nothing to change -- can
     * override this to skip needless redraws once it has drawn itself;
     * see WifiQrScreen, which exists to be held still in front of a
     * phone camera and was getting torn/banded by a rolling shutter from
     * being redrawn (and its QR re-encoded) every DisplayManager cycle.
     */
    virtual bool needsRedraw() const { return true; }
};

}  // namespace variometer
