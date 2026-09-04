#pragma once

#include "display/Screen.h"

namespace variometer {

/*
 * Right-hand footer field, ported from screen-spec.md's VarioField enum
 * for when field-cycling on a button press is wired up (see the spec's
 * open items) -- only VarioBarFooterField::AltAgl is implemented today;
 * GlideRatio has no data source yet (see formatFooterField() in the
 * .cpp) and the field is not currently switchable at all.
 *
 * Kept at namespace scope, not nested in VarioBarScreen, so the free
 * helper functions in VarioBarScreen.cpp's anonymous namespace (mirroring
 * the original vario_screen.cpp's own free-function structure) can name
 * it -- a private nested enum is not accessible outside class members.
 */
enum class VarioBarFooterField : uint8_t {
    GlideRatio,
    GroundSpeed,
    AltAgl,
    FlightTime,
};

/*
 * Bar-and-big-figure vario screen, ported from the standalone
 * vario_screen.h/.cpp design (see screen-spec.md) onto this project's
 * actual display stack. Now the default/initial screen shown on
 * power-up and the sole preflight screen -- the simpler text-only
 * VarioScreen it originally sat alongside has since been removed.
 *
 * Deliberate substitutions made while porting (see screen-spec.md's
 * "Ported to this codebase" section for the full reasoning):
 *   - No status strip. DisplayManager::drawCommonStatusBar() already
 *     draws satellites/battery for every screen; the spec's own strip
 *     would duplicate and visually collide with it.
 *   - Built-in GLCD font at sizes 1/3/4 in place of the spec's four
 *     U8g2 fonts, none of which exist in this codebase's font format
 *     (Adafruit GFX uses a different font representation entirely).
 *   - Right-aligned text is positioned by strlen() * a fixed per-size
 *     advance width, since SimpleDisplay has no getTextBounds() or
 *     getStrWidth() equivalent.
 *   - The dashed 30s-average marker on the bar chooses solid black or
 *     white by testing whether its y falls inside the filled region,
 *     rather than an XOR draw mode.
 *   - All non-font, non-status layout coordinates are unchanged from
 *     screen-spec.md. Adafruit GFX's built-in font positions text from
 *     the top-left of the glyph cell, not the baseline U8g2 used, so
 *     text sits somewhat lower on screen than the original design
 *     intended -- see screen-spec.md for the resulting pixel offsets.
 */
class VarioBarScreen : public Screen {
public:
    void enter() override;
    void update(const FlightData& data) override;
    void draw(DisplayManager& display, const FlightData& data) override;
    void exit() override;

private:
    void drawBar(DisplayManager& display, const FlightData& data) const;
    void drawNumeric(DisplayManager& display, const FlightData& data) const;
    void drawFooter(DisplayManager& display, const FlightData& data) const;

    VarioBarFooterField footerField_ = VarioBarFooterField::AltAgl;

    /*
     * TEMPORARY -- for filming this screen to show UI layout bugs without
     * rolling-shutter banding from the SH1107's row-scan rate (see
     * WifiQrScreen.cpp for the original writeup of why this works). Unlike
     * WifiQrScreen this screen redraws every cycle on purpose (live vario
     * data), so only the panel-side clock-div speedup applies here, not
     * WifiQrScreen's "draw once" trick. Remove fastClockApplied_,
     * lastDisplay_, and their use in enter()/draw()/exit() once the video
     * capture is done.
     */
    bool fastClockApplied_ = false;
    DisplayManager* lastDisplay_ = nullptr;
};

}  // namespace variometer
