#include "display/VarioScreen.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>

#include "config/Config.h"
#include "display/DisplayManager.h"

namespace variometer {
namespace {

// ---------------------------------------------------------------------
// Geometry -- every value here is taken directly from screen-spec.md's
// layout table and left unchanged, per the porting brief. Only the
// fonts and the status strip differ from the original design.
// ---------------------------------------------------------------------

constexpr int16_t kScreenW = 128;
constexpr int16_t kScreenH = 128;

// Climb bar
constexpr int16_t kBarX = 2;
constexpr int16_t kBarY = 14;
constexpr int16_t kBarW = 14;
constexpr int16_t kBarH = 78;
constexpr int16_t kBarZeroY = kBarY + kBarH / 2;  // 54
constexpr float kBarPxPerMs = (kBarH / 2.0f) / Config::VARIO_BAR_SCALE_MS;  // 7.8

// Numeric block
constexpr int16_t kBigRight = 124;
constexpr int16_t kBigBaseline = 66;

// Average row
constexpr int16_t kAvgBaseline = 88;
constexpr int16_t kAvgLabelX = 22;
constexpr int16_t kAvgValueX = 42;

// Footer
constexpr int16_t kFootRuleY = 94;
constexpr int16_t kFootDividerX = 64;
constexpr int16_t kFootLeftX = 4;
constexpr int16_t kFootRightX = 68;

// Right edges for the two right-justified footer values (drawRight()),
// each 4px clear of the boundary it sits against -- the divider for the
// left column, the panel edge (matching kBigRight's own margin) for the
// right one. Labels stay left-aligned; only the values, which vary in
// digit count, need this to avoid running off their column.
constexpr int16_t kFootLeftValueRight = kFootDividerX - 4;
constexpr int16_t kFootRightValueRight = kBigRight;

/*
 * kFootLabelY/kFootValueY are baseline positions, like every other
 * y-coordinate on this screen (see baselineToTopLeft() below) -- but
 * unlike the rest, their values differ from screen-spec.md's 106/124.
 * The built-in GLCD font's glyph height is 7px * size, so the footer
 * VALUE at size 3 is 21px tall -- taller than the spec's 18px gap
 * between its label and value baselines (124 - 106), which was sized for
 * whatever shorter font the original U8g2 design intended for that row.
 * No baseline/top-left conversion fixes that; the two rows need enough
 * room between them regardless of positioning convention.
 *
 * These lay the footer out to exactly fill what's actually available
 * between the rule (94) and the panel's bottom edge (128), all 34px of
 * it, with a 2px gap on both sides of the label: rule -> +2 -> label
 * (7px) -> +2 -> value (21px) -> +2 -> panel edge. That accounts for
 * every pixel: 2+7+2+21+2 = 34.
 */
constexpr int16_t kFootLabelY = 103;
constexpr int16_t kFootValueY = 126;

/*
 * Fonts -- screen-spec.md's four U8g2 fonts collapse to three sizes of
 * Adafruit GFX's built-in GLCD font, per the porting brief:
 *   size 1 -> status/field labels AND the "AVG"/"m/s"/30s-average-value
 *             row (the spec's fourth, intermediate-size font -- not
 *             explicitly assigned a size in the brief, grouped here
 *             with labels since it is a secondary reading, not the
 *             primary figure)
 *   size 3 -> footer values
 *   size 4 -> the big climb-rate figure
 *
 * The built-in font's advance is a fixed 6px wide / 8px tall per
 * character at size 1, scaling linearly with size -- used below to
 * right-align text by strlen() * advance instead of getTextBounds(),
 * which SimpleDisplay does not expose.
 */
constexpr int16_t kFontAdvanceW = 6;
constexpr int16_t kSmlLabelSize = 1;
constexpr int16_t kLrgLabelSize = 2;
constexpr int16_t kFooterValueSize = 3;
constexpr int16_t kBigFigureSize = 4;

#ifdef DEBUG
// TEMPORARY -- see the matching members in VarioScreen.h. Same SH1107
// SETDISPLAYCLOCKDIV (0xD5) values as WifiQrScreen.cpp, which has the
// full writeup of why this reduces rolling-shutter camera banding:
// divide-by-1 (0x50) doubles the panel's row-scan rate versus the
// vendor-default divide-by-2 (0x51) that Adafruit_SH1107::begin() leaves
// it at.
constexpr uint8_t kFastClockDiv = 0x50;
constexpr uint8_t kNormalClockDiv = 0x51;
#endif

int16_t textWidth(const char* s, int16_t size) {
    return static_cast<int16_t>(strlen(s)) * kFontAdvanceW * size;
}

// Shrinks a footer value from kFooterValueSize down to kLrgLabelSize once
// it's longer than 3 characters, so a wider reading (e.g. a 3-digit-plus
// altitude, or "L/D" going negative) still fits its column instead of
// running past kFootLeftValueRight/kFootRightValueRight regardless of
// the right-justification in drawRight().
int16_t footerValueSize(const char* s) {
    return (strlen(s) <= 3) ? kFooterValueSize : kLrgLabelSize;
}

/*
 * screen-spec.md's y-coordinates are baseline positions (bottom of the
 * glyph -- this font has no descenders), inherited from the original
 * U8g2/custom-GFXfont design where setCursor()'s y already means
 * baseline. The built-in classic GLCD font used here positions text from
 * the TOP-LEFT of the glyph cell instead, at every size -- not just for
 * labels. ("The big figure and footer values... need custom fonts
 * anyway, their y-coordinates transfer unchanged" was only ever true
 * once a custom font actually ships; stage one still uses the built-in
 * font for all three sizes, so all three need this.) A glyph is 7px tall
 * per unit of size, so the correction is a constant 7 * size regardless
 * of which constant it's applied to.
 */
constexpr int16_t kGlyphHeightPerSize = 7;

int16_t baselineToTopLeft(int16_t baselineY, int16_t size) {
    return baselineY - kGlyphHeightPerSize * size;
}

void drawRight(DisplayManager& display, int16_t right, int16_t baselineY,
              int16_t size, const char* s, uint16_t color) {
    display.display().setTextSize(static_cast<uint8_t>(size));
    display.display().setTextColor(color);
    display.display().setCursor(right - textWidth(s, size), baselineToTopLeft(baselineY, size));
    display.display().print(s);
}

// Dotted rules read as a lower tier than solid ones -- the substitute
// for grey on a 1-bit panel.
void dottedHLine(DisplayManager& display, int16_t x, int16_t y, int16_t w) {
    for (int16_t i = 0; i < w; i += 2) {
        display.display().drawPixel(x + i, y, SH110X_WHITE);
    }
}

void dottedVLine(DisplayManager& display, int16_t x, int16_t y, int16_t h) {
    for (int16_t i = 0; i < h; i += 2) {
        display.display().drawPixel(x, y + i, SH110X_WHITE);
    }
}

float applyDeadBand(float v) {
    return (fabsf(v) < Config::VARIO_BAR_DEAD_BAND_MS) ? 0.0f : v;
}

float clampToScale(float v) {
    if (v > Config::VARIO_BAR_SCALE_MS) return Config::VARIO_BAR_SCALE_MS;
    if (v < -Config::VARIO_BAR_SCALE_MS) return -Config::VARIO_BAR_SCALE_MS;
    return v;
}

// Signed one-decimal format, always with an explicit sign glyph. Without
// the sign the only cue for direction is the bar, and a glance that
// lands on the number first would be ambiguous.
void formatSigned(char* buf, size_t n, float v) {
    snprintf(buf, n, "%+.1f", static_cast<double>(v));
}

const char* footerFieldLabel(VarioBarFooterField f) {
    switch (f) {
        case VarioBarFooterField::GlideRatio:  return "L/D";
        case VarioBarFooterField::GroundSpeed: return "GS km/h";
        case VarioBarFooterField::AltAgl:      return "AGL m";
        case VarioBarFooterField::FlightTime:  return "TIME min";
        case VarioBarFooterField::Count:       break;  // sentinel, unreachable
    }
    return "";
}

void formatFooterField(char* buf, size_t n, VarioBarFooterField f,
                       const FlightData& data) {
    switch (f) {
        case VarioBarFooterField::GlideRatio:
            /*
             * Unimplemented: needs groundSpeed / -verticalSpeed, which
             * is undefined (or meaningless) while climbing or level --
             * deferred per the porting brief until there is a real
             * requirement to cycle to this field. See screen-spec.md.
             */
            snprintf(buf, n, "--");
            break;
        case VarioBarFooterField::GroundSpeed:
            snprintf(buf, n, "%d", static_cast<int>(lroundf(data.groundSpeed * 3.6f)));
            break;
        case VarioBarFooterField::AltAgl:
            snprintf(buf, n, "%d", static_cast<int>(lroundf(data.relativeAltitude)));
            break;
        case VarioBarFooterField::FlightTime: {
            // Minutes only, not H:MM -- at this footer slot's size-3 font
            // and 60px available width (kFootRightX to the screen edge),
            // "H:MM" needs 4 characters (72px) and ran off the panel,
            // rendering as a truncated "0:0". Minutes alone tops out at 3
            // digits for any realistic flight duration (54px).
            const uint32_t mins = data.flightDuration / 60;
            snprintf(buf, n, "%lu", static_cast<unsigned long>(mins));
            break;
        }
        case VarioBarFooterField::Count:
            break;  // sentinel, unreachable
    }
}

}  // namespace

void VarioBarScreen::enter() {
#ifdef DEBUG
    fastClockApplied_ = false;  // TEMPORARY -- see VarioScreen.h
#endif
    DBGLN("Entering vario bar screen");
}

void VarioBarScreen::update(const FlightData& data) {
    (void)data;
}

/*
 * Climb bar: a hard zero line, a fill that grows from it in the sign's
 * direction, and a dashed 30s-average marker on the same axis so
 * comparing it against instantaneous climb is spatial, not arithmetic.
 */
void VarioBarScreen::drawBar(DisplayManager& display, const FlightData& data) const {
    const float climb = clampToScale(applyDeadBand(data.verticalSpeed));
    const float avg = clampToScale(applyDeadBand(data.verticalSpeedAverage30s));

    display.display().drawRect(kBarX, kBarY, kBarW, kBarH, SH110X_WHITE);

    // Quarter-scale ticks, outside the track so they never collide with
    // the fill.
    const int16_t tick = static_cast<int16_t>(
        lroundf(Config::VARIO_BAR_SCALE_MS / 2.0f * kBarPxPerMs));
    display.display().drawLine(kBarX + kBarW, kBarZeroY - tick,
                               kBarX + kBarW + 3, kBarZeroY - tick, SH110X_WHITE);
    display.display().drawLine(kBarX + kBarW, kBarZeroY + tick,
                               kBarX + kBarW + 3, kBarZeroY + tick, SH110X_WHITE);

    // Fill grows from a hard zero line. Direction is the only free
    // channel for sign on a mono panel, so the centre reference has to
    // be unmistakable.
    int16_t h = static_cast<int16_t>(lroundf(fabsf(climb) * kBarPxPerMs));
    if (h < 1) h = 1;
    const int16_t fillTop = (climb > 0.0f) ? static_cast<int16_t>(kBarZeroY - h) : kBarZeroY;
    display.display().fillRect(kBarX + 1, fillTop, kBarW - 2, h, SH110X_WHITE);

    display.display().drawLine(0, kBarZeroY, kBarX + kBarW + 4, kBarZeroY, SH110X_WHITE);

    /*
     * 30s average as a dashed marker on the same axis. screen-spec.md's
     * original design used an XOR draw mode so the marker stayed
     * visible whether it fell inside the white fill or the black
     * background; SimpleDisplay has no draw-mode concept, so instead
     * this tests whether the marker's y lands inside the filled region
     * and picks solid black (visible against the white fill) or solid
     * white (visible against the black background) accordingly.
     */
    const int16_t avgY = static_cast<int16_t>(
        kBarZeroY - lroundf(avg * kBarPxPerMs));
    const bool avgInsideFill = (avgY >= fillTop) && (avgY < fillTop + h);
    const uint16_t dashColor = avgInsideFill ? SH110X_BLACK : SH110X_WHITE;

    for (int16_t x = kBarX; x < kBarX + kBarW; x += 3) {
        display.display().drawPixel(x, avgY, dashColor);
        display.display().drawPixel(x + 1, avgY, dashColor);
    }
}

/*
 * Big instantaneous climb figure, plus the "AVG"/value/"m/s" row below.
 *
 * A strong-sink inversion (solid white panel behind the figure, text
 * flipped to black) used to live here; removed because on this screen's
 * left-hand climb/sink bar is the sink indicator, and the panel -- at
 * ~30% of the screen -- read as a rendering glitch rather than a UI
 * element on top of it.
 */
void VarioBarScreen::drawNumeric(DisplayManager& display, const FlightData& data) const {
    char buf[12];
    const float climb = applyDeadBand(data.verticalSpeed);

    formatSigned(buf, sizeof(buf), clampToScale(climb)); //clamp to +/-5.0
    drawRight(display, kBigRight, kBigBaseline, kBigFigureSize, buf, SH110X_WHITE);

    display.display().setTextColor(SH110X_WHITE);
    display.display().setTextSize(kSmlLabelSize);
    display.display().setCursor(kAvgLabelX, baselineToTopLeft(kAvgBaseline, kSmlLabelSize));
    display.display().print("AVG");
    drawRight(display, kBigRight, kAvgBaseline, kLrgLabelSize, "m/s", SH110X_WHITE);

    formatSigned(buf, sizeof(buf), applyDeadBand(data.verticalSpeedAverage30s));
    display.display().setTextSize(kSmlLabelSize);
    display.display().setTextColor(SH110X_WHITE);
    display.display().setCursor(kAvgValueX, baselineToTopLeft(kAvgBaseline, kSmlLabelSize));
    display.display().print(buf);
}

void VarioBarScreen::drawFooter(DisplayManager& display, const FlightData& data) const {
    char buf[12];
    dottedHLine(display, 0, kFootRuleY, kScreenW);
    dottedVLine(display, kFootDividerX, kFootRuleY + 4, kScreenH - kFootRuleY - 6);

    display.display().setTextColor(SH110X_WHITE);
    display.display().setTextSize(kSmlLabelSize);
    display.display().setCursor(kFootLeftX, baselineToTopLeft(kFootLabelY, kSmlLabelSize));
    display.display().print("ALT m");
    display.display().setCursor(kFootRightX, baselineToTopLeft(kFootLabelY, kSmlLabelSize));
    display.display().print(footerFieldLabel(footerField_));

    snprintf(buf, sizeof(buf), "%d", static_cast<int>(lroundf(data.barometricAltitude)));
    drawRight(display, kFootLeftValueRight, kFootValueY, footerValueSize(buf), buf, SH110X_WHITE);

    formatFooterField(buf, sizeof(buf), footerField_, data);
    drawRight(display, kFootRightValueRight, kFootValueY, footerValueSize(buf), buf, SH110X_WHITE);
}

void VarioBarScreen::draw(DisplayManager& display, const FlightData& data) {
#ifdef DEBUG
    // TEMPORARY -- see VarioScreen.h. Sent once per visit, not every
    // redraw, even though this screen redraws every cycle (unlike
    // WifiQrScreen, still wanted live here for the video).
    lastDisplay_ = &display;
    if (!fastClockApplied_) {
        display.display().sendCommand(SH110X_SETDISPLAYCLOCKDIV, kFastClockDiv);
        fastClockApplied_ = true;
    }
#endif

    display.display().setTextColor(SH110X_WHITE);
    drawBar(display, data);
    drawNumeric(display, data);
    drawFooter(display, data);
    display.display().setTextColor(SH110X_WHITE);
    display.display().setTextSize(1);
}

void VarioBarScreen::exit() {
#ifdef DEBUG
    // TEMPORARY -- see VarioScreen.h
    if (lastDisplay_ != nullptr) {
        lastDisplay_->display().sendCommand(SH110X_SETDISPLAYCLOCKDIV, kNormalClockDiv);
    }
#endif
}

void VarioBarScreen::cycleFooterField() {
    const uint8_t next = static_cast<uint8_t>(footerField_) + 1;
    footerField_ = static_cast<VarioBarFooterField>(next % static_cast<uint8_t>(VarioBarFooterField::Count));
}

}  // namespace variometer
