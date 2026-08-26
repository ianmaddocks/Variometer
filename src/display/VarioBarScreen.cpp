#include "display/VarioBarScreen.h"

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
constexpr int16_t kBarY = 15;
constexpr int16_t kBarW = 14;
constexpr int16_t kBarH = 78;
constexpr int16_t kBarZeroY = kBarY + kBarH / 2;  // 54
constexpr float kBarPxPerMs = (kBarH / 2.0f) / Config::VARIO_BAR_SCALE_MS;  // 7.8

// Numeric block
constexpr int16_t kBigRight = 124;
constexpr int16_t kBigBaseline = 66;
constexpr int16_t kInvX = 20;
constexpr int16_t kInvY = 26;
constexpr int16_t kInvW = 106;
constexpr int16_t kInvH = 46;

// Average row
constexpr int16_t kAvgBaseline = 88;
constexpr int16_t kAvgLabelX = 22;
constexpr int16_t kAvgValueX = 42;

// Footer
constexpr int16_t kFootRuleY = 94;
constexpr int16_t kFootDividerX = 64;
constexpr int16_t kFootLabelY = 106;
constexpr int16_t kFootValueY = 124;
constexpr int16_t kFootLeftX = 4;
constexpr int16_t kFootRightX = 68;

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
constexpr int16_t kLabelSize = 1;
constexpr int16_t kFooterValueSize = 3;
constexpr int16_t kBigFigureSize = 4;

int16_t textWidth(const char* s, int16_t size) {
    return static_cast<int16_t>(strlen(s)) * kFontAdvanceW * size;
}

void drawRight(DisplayManager& display, int16_t right, int16_t y,
              int16_t size, const char* s, uint16_t color) {
    display.display().setTextSize(static_cast<uint8_t>(size));
    display.display().setTextColor(color);
    display.display().setCursor(right - textWidth(s, size), y);
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
        case VarioBarFooterField::FlightTime:  return "TIME";
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
            const uint32_t secs = data.flightDuration;
            snprintf(buf, n, "%lu:%02lu",
                     static_cast<unsigned long>(secs / 3600),
                     static_cast<unsigned long>((secs / 60) % 60));
            break;
        }
    }
}

}  // namespace

void VarioBarScreen::enter() {
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
 * Big instantaneous climb figure, with the invert-on-sink block behind
 * it, plus the "AVG"/value/"m/s" row below.
 */
void VarioBarScreen::drawNumeric(DisplayManager& display, const FlightData& data) const {
    char buf[12];
    const float climb = applyDeadBand(data.verticalSpeed);
    const bool invert = (climb < Config::VARIO_BAR_INVERT_BELOW_MS);

    if (invert) {
        display.display().fillRect(kInvX, kInvY, kInvW, kInvH, SH110X_WHITE);
    }

    formatSigned(buf, sizeof(buf), clampToScale(climb));
    drawRight(display, kBigRight, kBigBaseline, kBigFigureSize, buf,
             invert ? SH110X_BLACK : SH110X_WHITE);

    display.display().setTextColor(SH110X_WHITE);
    display.display().setTextSize(kLabelSize);
    display.display().setCursor(kAvgLabelX, kAvgBaseline);
    display.display().print("AVG");
    drawRight(display, kBigRight, kAvgBaseline, kLabelSize, "m/s", SH110X_WHITE);

    formatSigned(buf, sizeof(buf), applyDeadBand(data.verticalSpeedAverage30s));
    display.display().setTextSize(kLabelSize);
    display.display().setTextColor(SH110X_WHITE);
    display.display().setCursor(kAvgValueX, kAvgBaseline);
    display.display().print(buf);
}

void VarioBarScreen::drawFooter(DisplayManager& display, const FlightData& data) const {
    char buf[12];
    dottedHLine(display, 0, kFootRuleY, kScreenW);
    dottedVLine(display, kFootDividerX, kFootRuleY + 4, kScreenH - kFootRuleY - 6);

    display.display().setTextColor(SH110X_WHITE);
    display.display().setTextSize(kLabelSize);
    display.display().setCursor(kFootLeftX, kFootLabelY);
    display.display().print("ALT m");
    display.display().setCursor(kFootRightX, kFootLabelY);
    display.display().print(footerFieldLabel(footerField_));

    display.display().setTextSize(kFooterValueSize);
    snprintf(buf, sizeof(buf), "%d", static_cast<int>(lroundf(data.barometricAltitude)));
    display.display().setCursor(kFootLeftX, kFootValueY);
    display.display().print(buf);

    formatFooterField(buf, sizeof(buf), footerField_, data);
    display.display().setCursor(kFootRightX, kFootValueY);
    display.display().print(buf);
}

void VarioBarScreen::draw(DisplayManager& display, const FlightData& data) {
    display.display().setTextColor(SH110X_WHITE);
    drawBar(display, data);
    drawNumeric(display, data);
    drawFooter(display, data);
    display.display().setTextColor(SH110X_WHITE);
    display.display().setTextSize(1);
}

void VarioBarScreen::exit() {}

}  // namespace variometer
