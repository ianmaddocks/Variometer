#include "display/WindDirectionScreen.h"

#include <Arduino.h>
#include <math.h>

#include "display/DisplayHelpers.h"
#include "display/DisplayManager.h"

namespace variometer {
namespace {

/*
 * Layout.
 *
 * Content sits between the common status bar (which owns the top 12
 * rows) and, in debug builds, the footer line at y = 110. The wind rose
 * takes the left two thirds; the vario gauge is a fixed column on the
 * right, as specified.
 */
constexpr int16_t kContentTop = 13;
#ifdef DEBUG
constexpr int16_t kContentBottom = 108;
#else
constexpr int16_t kContentBottom = 126;
#endif

constexpr int16_t kCircleCX = 38;
constexpr int16_t kCircleCY = (kContentTop + kContentBottom) / 2;
constexpr int16_t kCircleR = 31;

// Radius of the north marker and the LZ marker's centre: both sit
// directly on the wind circle's edge, matching radii so neither reads as
// more "inside" or "outside" the instrument than the other.
constexpr int16_t kCompassRadius = kCircleR;
constexpr int16_t kLzMarkerRadius = kCircleR;

/*
 * Background disc drawn behind "N" so it reads inverted (filled white,
 * black text) instead of plain white-on-black like the other three
 * points. Radius 5 circumscribes the letter's 6x8 bounding box (half
 * diagonal = 5) with no wasted margin, keeping it distinct without
 * growing large enough to merge into the wind circle's own outline.
 */
constexpr int16_t kNorthMarkerRadius = 6;

// Vario gauge: right edge flush to the screen, widening leftwards away
// from zero, per the design sketch.
constexpr int16_t kVarioRight = 126;
constexpr int16_t kVarioMaxW = 22;
constexpr int16_t kVarioCY = kCircleCY;
constexpr int16_t kVarioHalfH = 44;

// Numeric climb/sink readout, left of the gauge.
constexpr int16_t kVarioTextX = 74;

// Character cell of the default 5x7 font plus its 1px spacing.
constexpr int16_t kCharW = 6;
constexpr int16_t kCharH = 8;

constexpr float kDegToRad = 0.017453292519943295f;

// Below this the estimate is treated as meaningless and no arrow is
// drawn -- an arrow pointing nowhere in particular is worse than none.
constexpr float kMinConfidence = 0.2f;

#ifdef DEBUG
String lastWindStatus;
#endif

int16_t clampI16(int32_t v, int16_t lo, int16_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return static_cast<int16_t>(v);
}

// Draws text centred on (cx, cy) rather than anchored at a corner, so
// labels can be placed against a shape's centroid.
void drawCentredText(DisplayManager& display, int16_t cx, int16_t cy,
                     const char* text, uint16_t colour) {
    const int16_t len = static_cast<int16_t>(strlen(text));
    const int16_t x = cx - ((len * kCharW) / 2);
    const int16_t y = cy - (kCharH / 2);

    display.display().setTextColor(colour);
    display.display().setCursor(x, y);
    display.display().print(text);
    display.display().setTextColor(SH110X_WHITE);
}

/*
 * Reduce an absolute compass bearing to a screen bearing relative to the
 * current direction of travel.
 *
 * This is the one place "track-up" happens: everything drawn on the
 * rose -- wind arrow, compass letters, LZ marker -- goes through this
 * before being placed, so the whole rose rotates together and nothing
 * can be plotted north-up by accident.
 */
float relativeBearing(float absoluteDeg, float trackDeg) {
    return fmodf(absoluteDeg - trackDeg + 360.0f, 360.0f);
}

/*
 * Convert a screen bearing (0=up, clockwise) plus a forward/lateral
 * offset into a screen point centred on the wind circle.
 *
 * Shared by the wind arrow (which needs forward+lateral, to build the
 * dart's barbs) and anything that just needs a point at a bearing and
 * radius (the compass letters, the LZ marker) via lateral=0. One
 * rotation primitive, so the compass ring and the arrow cannot drift
 * out of agreement about which way is which.
 */
void bearingToPoint(float bearingDeg, float forward, float lateral,
                    int16_t* outX, int16_t* outY) {
    const float rad = bearingDeg * kDegToRad;
    const float sinB = sinf(rad);
    const float cosB = cosf(rad);

    const float dx = (forward * sinB) + (lateral * cosB);
    const float dy = -(forward * cosB) + (lateral * sinB);

    *outX = clampI16(static_cast<int32_t>(kCircleCX + dx), 0, 127);
    *outY = clampI16(static_cast<int32_t>(kCircleCY + dy), 0, 127);
}

}  // namespace

void WindDirectionScreen::enter() {
#ifdef DEBUG
    lastWindStatus = "";
#endif
    DBGLN("Entering wind direction screen");
}

void WindDirectionScreen::update(const FlightData& data) {
    (void)data;
}

/*
 * Filled arrowhead showing wind direction, with the speed written inside
 * it.
 *
 * Drawn as a dart: tip, two swept-back barbs, and a notch in the trailing
 * edge. Built from two triangles sharing the tip-to-notch edge.
 *
 * The barbs are swept only moderately (kBarbAngle) rather than forming a
 * narrow spike, because the label has to fit inside the fill at every
 * rotation. A slimmer, more elegant arrow would leave no room for the
 * text at the centre once rotated.
 *
 * screenBearingDeg is already track-relative (0=up=direction of travel);
 * the caller is responsible for that conversion, so this function has no
 * idea what "north" is and cannot accidentally draw north-up.
 */
void WindDirectionScreen::drawWindArrow(DisplayManager& display,
                                        float screenBearingDeg,
                                        float speedMs,
                                        float confidence) const {
    // Scale with confidence so a marginal estimate visibly carries less
    // weight, but never shrink below the size the label needs.
    const float scale = 0.78f + (0.22f * fminf(fmaxf(confidence, 0.0f), 1.0f));

    /*
     * Proportions are not cosmetic -- they are chosen so the speed label
     * survives rotation.
     *
     * The label is drawn horizontally while the dart rotates, so the
     * only region guaranteed to stay inside the fill is the largest
     * circle inscribed in the dart. For a concave arrowhead that circle
     * is small, and it is the concave trailing edges that limit it.
     * These values put it at ~12.9px (centred 3.3px forward of the
     * circle centre), which accommodates a 4-character single line at
     * 12.65px. A second line would need 14.7px and would spill outside
     * the fill, rendering black-on-black -- which is why the unit label
     * lives outside the circle instead.
     */
    const float tipLen = kCircleR * 0.95f * scale;
    const float barbLen = kCircleR * 0.98f * scale;
    const float notchLen = kCircleR * 0.32f * scale;
    /*
     * Must stay at 122 deg, not a cosmetic tweak: this angle was solved
     * for so the dart's inscribed radius (~12.9px, see comment above)
     * covers the 4-character label at 12.65px. A later change to 130
     * deg shrank that radius to ~11.2px at full confidence and ~9.2px
     * at the minimum drawn confidence, clipping readings of 10+ m/s
     * (reachable -- WIND_ESTIMATE_MAX_MS is 25) black-on-black outside
     * the fill. If the barb angle needs to change again, recompute the
     * inscribed radius against the label size first.
     */
    constexpr float kBarbAngle = 122.0f * kDegToRad;

    // Anchor for the label, along the arrow axis, at the inscribed centre.
    const float labelForward = kCircleR * 0.107f * scale;

    int16_t tipX, tipY, leftX, leftY, rightX, rightY, notchX, notchY;
    bearingToPoint(screenBearingDeg, tipLen, 0.0f, &tipX, &tipY);
    bearingToPoint(screenBearingDeg, barbLen * cosf(kBarbAngle),
                   barbLen * sinf(kBarbAngle), &leftX, &leftY);
    bearingToPoint(screenBearingDeg, barbLen * cosf(kBarbAngle),
                   -barbLen * sinf(kBarbAngle), &rightX, &rightY);
    bearingToPoint(screenBearingDeg, -notchLen, 0.0f, &notchX, &notchY);

    display.display().fillTriangle(tipX, tipY, leftX, leftY, notchX, notchY,
                                   SH110X_WHITE);
    display.display().fillTriangle(tipX, tipY, rightX, rightY, notchX, notchY,
                                   SH110X_WHITE);

    /*
     * Speed inside the arrowhead, black on the white fill, anchored at
     * the inscribed centre so it stays within the dart as it rotates.
     */
    char valueText[8];
    snprintf(valueText, sizeof(valueText), "%.1f", static_cast<double>(speedMs));

    int16_t labelX, labelY;
    bearingToPoint(screenBearingDeg, labelForward, 0.0f, &labelX, &labelY);

    drawCentredText(display, labelX, labelY, valueText, SH110X_BLACK);
}

/*
 * North marker, rotated so it always agrees with the wind arrow and LZ
 * marker about what "up" currently means.
 *
 * This replaces a fixed north tick from an earlier version of this
 * screen. A north-up tick is actively wrong once the rose is track-up:
 * "north" only points up when flying due north, and the tick would then
 * be redundant one moment and misleading the rest of the time it sat
 * still while everything around it rotated.
 *
 * S/E/W were dropped: on a heading-up rose N is the one reference a
 * pilot actually needs (everything else is read relative to the nose,
 * not by cardinal direction), and three more rotating labels only added
 * clutter without adding information.
 *
 * Drawn inverted (filled disc, black text) rather than plain
 * white-on-black so it can be picked out at a glance without reading it.
 */
void WindDirectionScreen::drawCompassRing(DisplayManager& display,
                                          float trackDeg) const {
    const float screenBearing = relativeBearing(0.0f, trackDeg);

    int16_t x, y;
    bearingToPoint(screenBearing, static_cast<float>(kCompassRadius), 0.0f, &x, &y);

    display.display().fillCircle(x, y, kNorthMarkerRadius, SH110X_WHITE);
    drawCentredText(display, x, y, "N", SH110X_BLACK);
}

/*
 * Vario gauge: two outlined triangles meeting at zero, the upper one
 * filling for climb and the lower for sink.
 *
 * Both have a flat right edge and widen away from zero, so the amount of
 * ink grows in two dimensions at once and the reading is legible at a
 * glance without being read as a number.
 */
void WindDirectionScreen::drawVarioGauge(DisplayManager& display,
                                         float verticalSpeed) const {
    // Outlines: apex at zero on the right edge, widening to full width at
    // full deflection.
    display.display().drawTriangle(kVarioRight, kVarioCY,
                                   kVarioRight, kVarioCY - kVarioHalfH,
                                   kVarioRight - kVarioMaxW, kVarioCY - kVarioHalfH,
                                   SH110X_WHITE);
    display.display().drawTriangle(kVarioRight, kVarioCY,
                                   kVarioRight, kVarioCY + kVarioHalfH,
                                   kVarioRight - kVarioMaxW, kVarioCY + kVarioHalfH,
                                   SH110X_WHITE);

    // Zero reference, extended slightly left of the gauge so it reads as
    // a baseline rather than part of the triangles.
    display.display().drawLine(kVarioRight - kVarioMaxW - 3, kVarioCY,
                               kVarioRight, kVarioCY, SH110X_WHITE);

    if (!isfinite(verticalSpeed)) {
        return;
    }

    const bool climbing = (verticalSpeed >= 0.0f);
    const float magnitude = fabsf(verticalSpeed);

    // Clamp to full scale; the numeric readout still shows the truth.
    const float fraction =
        fminf(magnitude / Config::VARIO_DISPLAY_SCALE_MS, 1.0f);

    const int16_t fillRows =
        static_cast<int16_t>(fraction * static_cast<float>(kVarioHalfH));

    // Rows per graduation, used to leave a 1px unfilled gap at each
    // 0.5 m/s boundary so the fill can be read as a value.
    const float rowsPerGraduation =
        (Config::VARIO_DISPLAY_GRADUATION_MS /
         Config::VARIO_DISPLAY_SCALE_MS) * static_cast<float>(kVarioHalfH);

    for (int16_t row = 1; row <= fillRows; ++row) {
        // Leave the graduation boundaries clear.
        if (rowsPerGraduation >= 2.0f) {
            const float pos = static_cast<float>(row) / rowsPerGraduation;
            const float frac = pos - floorf(pos);
            if (frac < (1.0f / rowsPerGraduation)) {
                continue;
            }
        }

        // Width grows linearly with distance from zero, matching the
        // triangle outline.
        const int16_t width = static_cast<int16_t>(
            (static_cast<float>(row) / static_cast<float>(kVarioHalfH)) *
            static_cast<float>(kVarioMaxW));

        if (width <= 0) {
            continue;
        }

        const int16_t y = climbing ? (kVarioCY - row) : (kVarioCY + row);

        if (y < kContentTop || y > kContentBottom) {
            continue;
        }

        display.display().drawLine(kVarioRight - width, y, kVarioRight, y,
                                   SH110X_WHITE);
    }
}

void WindDirectionScreen::draw(DisplayManager& display, const FlightData& data) {
    const bool haveWind = (data.windConfidence > kMinConfidence);

    // See VarioScreen.cpp for why this is compiled out entirely in
    // release builds rather than left as a DBGLN no-op.
#ifdef DEBUG
    String status = "Wind: " + String(haveWind ? data.windSpeed : 0.0f, 1) + "m/s " +
                    String(static_cast<int>(data.windDirection)) + "deg conf " +
                    String(static_cast<int>(data.windConfidence * 100.0f)) +
                    "% track " + String(static_cast<int>(data.track)) + "deg";
    if (status != lastWindStatus) {
        lastWindStatus = status;
        DBGLN(status);
    }
#endif

    // Confidence folded into the header rather than placed on the rose
    // itself: the top of the circle is now live space that the compass
    // ring and the wind arrow both rotate through.
    display.display().setCursor(0, 1);
    display.display().print("Wind ");
    display.display().print(static_cast<int>(data.windConfidence * 100.0f));
    display.display().print("%");

    display.display().drawCircle(kCircleCX, kCircleCY, kCircleR, SH110X_WHITE);

    /*
     * Everything below is drawn relative to the direction of travel, not
     * geographic north: "up" on this rose is always where the aircraft
     * is currently headed, and the whole rose -- compass letters, wind
     * arrow, LZ marker -- turns together as track changes. This is the
     * same convention as a car GPS in "heading up" mode, and matches
     * this screen's original spec describing the wind arrow itself as
     * "relative wind direction".
     */
    drawCompassRing(display, data.track);

    if (data.hasLz) {
        const float lzScreenBearing = relativeBearing(data.bearingToLZ, data.track);

        int16_t lzX, lzY;
        bearingToPoint(lzScreenBearing, static_cast<float>(kLzMarkerRadius), 0.0f,
                       &lzX, &lzY);

        display_helpers::drawLzMarker(display, lzX, lzY);
    }

    if (haveWind) {
        const float windScreenBearing = relativeBearing(data.windDirection, data.track);
        drawWindArrow(display, windScreenBearing, data.windSpeed, data.windConfidence);
    } else {
        // Explicitly state the estimate is unavailable rather than
        // leaving an empty circle that could read as "calm".
        drawCentredText(display, kCircleCX, kCircleCY, "--.-", SH110X_WHITE);
    }

    /*
     * Unit label sits below the circle rather than beside the number
     * inside the arrow: a second line of text does not fit within the
     * dart's inscribed area once it rotates (see drawWindArrow), and a
     * label that vanishes at certain bearings is worse than one placed
     * consistently outside.
     */
    drawCentredText(display, kCircleCX, kCircleCY + kCircleR + 12, "m/s",
                    SH110X_WHITE);

    drawVarioGauge(display, data.verticalSpeed);

    // Numeric climb/sink beside the gauge.
    char varioText[8];
    snprintf(varioText, sizeof(varioText), "%+.1f",
             static_cast<double>(data.verticalSpeed));

    drawCentredText(display, kVarioTextX + 12, kVarioCY - 5, varioText,
                    SH110X_WHITE);
    drawCentredText(display, kVarioTextX + 12, kVarioCY + 5, "m/s",
                    SH110X_WHITE);
}

void WindDirectionScreen::exit() {}

}  // namespace variometer
