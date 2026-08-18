#include "display/FlightMapScreen.h"

#include <Arduino.h>
#include <math.h>

#include "display/DisplayHelpers.h"
#include "display/DisplayManager.h"
#include "flight/FlightTrack.h"

namespace variometer {
namespace {

/*
 * Plot area, inset between the status bar and the debug footer so the
 * map never collides with either.
 */
constexpr int16_t kPlotX = 0;
constexpr int16_t kPlotY = 13;
constexpr int16_t kPlotW = 128;
#ifdef DEBUG
constexpr int16_t kPlotH = 95;
#else
constexpr int16_t kPlotH = 108;
#endif

constexpr int16_t kPlotRight = kPlotX + kPlotW - 1;
constexpr int16_t kPlotBottom = kPlotY + kPlotH - 1;
constexpr int16_t kPlotCentreX = kPlotX + (kPlotW / 2);
constexpr int16_t kPlotCentreY = kPlotY + (kPlotH / 2);

/*
 * Fraction of the plot left empty around the track.
 *
 * Without this the extremes of the flight land exactly on the border and
 * the aircraft marker gets clipped as it reaches the edge.
 */
constexpr float kPaddingFraction = 0.12f;

// Smallest span the viewport will scale to. Prevents a stationary
// aircraft being magnified until GPS jitter fills the screen.
constexpr float kMinSpanMetres = 120.0f;

// Track extent beyond which the map stops fitting everything and starts
// following the aircraft instead.
constexpr float kFollowThresholdM = 1200.0f;

// Span shown when following the aircraft.
constexpr float kFollowSpanM = 800.0f;

// While following, briefly show the whole flight at this cadence so the
// overall shape is not lost.
constexpr uint32_t kOverviewIntervalMs = 15000;
constexpr uint32_t kOverviewDurationMs = 3000;

// Number of trailing segments drawn emphasised so the head of the track
// reads clearly against the rest of the line.
constexpr size_t kHeadSegments = 6;

int16_t clampInt16(int32_t value, int16_t low, int16_t high) {
    if (value < low) return low;
    if (value > high) return high;
    return static_cast<int16_t>(value);
}

/*
 * Cohen-Sutherland clipping against the plot rectangle.
 *
 * Necessary because Adafruit_GFX clips to the physical screen, not to
 * our plot area: an unclipped track segment would happily draw straight
 * through the status bar and the footer. Simply clamping the endpoints
 * is not good enough either -- that bends the segment and paints a false
 * line along the border. Proper clipping keeps the geometry honest.
 *
 * Returns false if the segment lies entirely outside the plot.
 */
constexpr uint8_t kInside = 0;
constexpr uint8_t kLeft = 1;
constexpr uint8_t kRight = 2;
constexpr uint8_t kBottom = 4;
constexpr uint8_t kTop = 8;

uint8_t outCode(float x, float y) {
    uint8_t code = kInside;

    if (x < static_cast<float>(kPlotX)) {
        code |= kLeft;
    } else if (x > static_cast<float>(kPlotRight)) {
        code |= kRight;
    }

    if (y < static_cast<float>(kPlotY)) {
        code |= kTop;
    } else if (y > static_cast<float>(kPlotBottom)) {
        code |= kBottom;
    }

    return code;
}

bool clipToPlot(float& x0, float& y0, float& x1, float& y1) {
    uint8_t code0 = outCode(x0, y0);
    uint8_t code1 = outCode(x1, y1);

    // Bounded to keep a degenerate case from spinning; four boundaries
    // means four iterations is always sufficient.
    for (int guard = 0; guard < 8; ++guard) {
        if ((code0 | code1) == 0) {
            return true;  // wholly inside
        }

        if ((code0 & code1) != 0) {
            return false;  // wholly outside one edge
        }

        const uint8_t code = (code0 != 0) ? code0 : code1;

        float x = 0.0f;
        float y = 0.0f;

        if (code & kBottom) {
            y = static_cast<float>(kPlotBottom);
            x = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
        } else if (code & kTop) {
            y = static_cast<float>(kPlotY);
            x = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
        } else if (code & kRight) {
            x = static_cast<float>(kPlotRight);
            y = y0 + (y1 - y0) * (x - x0) / (x1 - x0);
        } else {
            x = static_cast<float>(kPlotX);
            y = y0 + (y1 - y0) * (x - x0) / (x1 - x0);
        }

        if (code == code0) {
            x0 = x;
            y0 = y;
            code0 = outCode(x0, y0);
        } else {
            x1 = x;
            y1 = y;
            code1 = outCode(x1, y1);
        }
    }

    return false;
}

// Draw a world-space segment, clipped to the plot area.
void drawClippedLine(DisplayManager& display,
                     float x0, float y0, float x1, float y1) {
    if (!clipToPlot(x0, y0, x1, y1)) {
        return;
    }

    display.display().drawLine(static_cast<int16_t>(x0), static_cast<int16_t>(y0),
                               static_cast<int16_t>(x1), static_cast<int16_t>(y1),
                               SH110X_WHITE);
}

/*
 * Round a distance down to a 1/2/5 x 10^n value so the scale bar always
 * shows a number a pilot can reason about.
 */
float niceScaleLength(float metres) {
    if (metres <= 0.0f) {
        return 1.0f;
    }

    float magnitude = 1.0f;

    while (metres >= 10.0f) {
        metres /= 10.0f;
        magnitude *= 10.0f;
    }
    while (metres < 1.0f) {
        metres *= 10.0f;
        magnitude /= 10.0f;
    }

    float rounded = 1.0f;
    if (metres >= 5.0f) {
        rounded = 5.0f;
    } else if (metres >= 2.0f) {
        rounded = 2.0f;
    }

    return rounded * magnitude;
}

}  // namespace

void FlightMapScreen::enter() {
    DBGLN("Entering flight map screen");

    // Always open in the plan view; 3D is an explicit choice.
    viewMode_ = ViewMode::TopDown;
    overviewActive_ = false;
    lastOverviewMs_ = millis();
    replayRunning_ = false;
}

void FlightMapScreen::exit() {}

void FlightMapScreen::setReplaySpeed(uint8_t speed) {
    replaySpeed_ = (speed == 0) ? 1 : speed;
}

bool FlightMapScreen::canEnterThreeD(FlightState state) const {
    /*
     * Only after landing. In flight the pilot needs an unambiguous plan
     * view, and a rotating projection is the wrong thing to be reading
     * while airborne.
     */
    return (state == FlightState::POST_FLIGHT ||
            state == FlightState::LANDING_DETECTED) &&
           track_ != nullptr && track_->size() >= 2;
}

void FlightMapScreen::toggleViewMode(FlightState state) {
    if (viewMode_ == ViewMode::ThreeD) {
        viewMode_ = ViewMode::TopDown;
        replayRunning_ = false;
        DBGLN("Flight map: back to plan view");
        return;
    }

    if (!canEnterThreeD(state)) {
        DBGLN("Flight map: 3D view unavailable (needs a completed flight)");
        return;
    }

    viewMode_ = ViewMode::ThreeD;

    // Entering 3D restarts the replay from takeoff.
    replayStartMs_ = millis();
    replayRunning_ = true;

    DBGF("Flight map: 3D replay at %ux\n", static_cast<unsigned>(replaySpeed_));
}

void FlightMapScreen::rotateView(int8_t delta) {
    azimuthDeg_ += static_cast<float>(delta) * 15.0f;

    while (azimuthDeg_ >= 360.0f) azimuthDeg_ -= 360.0f;
    while (azimuthDeg_ < 0.0f) azimuthDeg_ += 360.0f;
}

void FlightMapScreen::update(const FlightData& data) {
    if (track_ == nullptr) {
        return;
    }

    const uint32_t now = millis();

    /*
     * Decide which zoom stage applies. Hysteresis is unnecessary here
     * because the track extent only ever grows during a flight, so the
     * mode cannot oscillate.
     */
    followMode_ =
        (static_cast<float>(track_->extentMetres()) > kFollowThresholdM);

    if (!followMode_) {
        overviewActive_ = false;
        return;
    }

    // Periodically pull back to the whole flight, then resume following.
    if (overviewActive_) {
        if ((now - overviewStartedMs_) >= kOverviewDurationMs) {
            overviewActive_ = false;
            lastOverviewMs_ = now;
        }
    } else if ((now - lastOverviewMs_) >= kOverviewIntervalMs) {
        overviewActive_ = true;
        overviewStartedMs_ = now;
    }
}

FlightMapScreen::Viewport FlightMapScreen::computeViewport(
    float liveEastM, float liveNorthM) const {
    Viewport viewport;
    viewport.metresPerPixel = 1.0f;
    viewport.centreEastM = 0.0f;
    viewport.centreNorthM = 0.0f;

    if (track_ == nullptr || track_->empty()) {
        return viewport;
    }

    const bool fitAll = (!followMode_ || overviewActive_);

    float spanEast = 0.0f;
    float spanNorth = 0.0f;

    if (fitAll) {
        /*
         * Fit the whole track plus the LZ. The LZ is at the origin by
         * construction, so including 0 in the bounds guarantees the
         * takeoff marker stays on screen.
         */
        const float minE = fminf(0.0f, static_cast<float>(track_->minEast()));
        const float maxE = fmaxf(0.0f, static_cast<float>(track_->maxEast()));
        const float minN = fminf(0.0f, static_cast<float>(track_->minNorth()));
        const float maxN = fmaxf(0.0f, static_cast<float>(track_->maxNorth()));

        viewport.centreEastM = (minE + maxE) * 0.5f;
        viewport.centreNorthM = (minN + maxN) * 0.5f;

        spanEast = maxE - minE;
        spanNorth = maxN - minN;
    } else {
        // Follow the aircraft with a fixed window, using the same
        // (possibly dead-reckoned) position as the marker itself, so the
        // view never centres on a different point than where the
        // aircraft is actually drawn.
        viewport.centreEastM = liveEastM;
        viewport.centreNorthM = liveNorthM;

        spanEast = kFollowSpanM;
        spanNorth = kFollowSpanM;
    }

    if (spanEast < kMinSpanMetres) spanEast = kMinSpanMetres;
    if (spanNorth < kMinSpanMetres) spanNorth = kMinSpanMetres;

    /*
     * Choose the scale from whichever axis is tighter, so the track fits
     * in both directions, then add padding. A single metresPerPixel for
     * both axes keeps the map geometrically honest -- distances read the
     * same whichever way the pilot flew.
     */
    const float usableW = static_cast<float>(kPlotW) * (1.0f - kPaddingFraction);
    const float usableH = static_cast<float>(kPlotH) * (1.0f - kPaddingFraction);

    const float scaleX = spanEast / usableW;
    const float scaleY = spanNorth / usableH;

    viewport.metresPerPixel = fmaxf(scaleX, fmaxf(scaleY, 0.05f));

    return viewport;
}

void FlightMapScreen::draw(DisplayManager& display, const FlightData& data) {
    display.display().setCursor(0, 1);
    display.display().print(viewMode_ == ViewMode::ThreeD ? "Flight 3D" : "Flight map");

    if (track_ == nullptr || track_->size() < 2) {
        drawEmptyState(display);
        return;
    }

    if (viewMode_ == ViewMode::ThreeD) {
        drawThreeD(display, data);
    } else {
        drawTopDown(display, data);
    }
}

void FlightMapScreen::drawEmptyState(DisplayManager& display) const {
    display.display().setCursor(14, kPlotCentreY);
    display.display().print("No track yet");
}

void FlightMapScreen::drawTopDown(DisplayManager& display,
                                  const FlightData& data) {
    /*
     * Current position, computed once so the viewport centring (in
     * follow mode) and the aircraft marker always agree on where "here"
     * is. Falls back through three tiers: a live/dead-reckoned estimate
     * from the track's own reference, then the last stored point if
     * there has never been a good fix at all (e.g. right after power-on
     * before any fix was acquired), which cannot happen once the track
     * has an origin and at least one point, but is handled for safety.
     */
    float liveEast = 0.0f;
    float liveNorth = 0.0f;
    bool deadReckoned = false;

    const bool haveEstimate =
        track_->getLiveEstimate(millis(), &liveEast, &liveNorth, &deadReckoned);

    if (!haveEstimate) {
        const TrackPoint& last = track_->at(track_->size() - 1);
        liveEast = static_cast<float>(last.eastM);
        liveNorth = static_cast<float>(last.northM);
    }

    const Viewport viewport = computeViewport(liveEast, liveNorth);
    const float invScale = 1.0f / viewport.metresPerPixel;

    /*
     * World-to-screen. North is always up and the map never rotates:
     * +north decreases screen Y, +east increases screen X.
     */
    const auto toScreenX = [&](float eastM) -> float {
        return static_cast<float>(kPlotCentreX) +
               ((eastM - viewport.centreEastM) * invScale);
    };
    const auto toScreenY = [&](float northM) -> float {
        return static_cast<float>(kPlotCentreY) -
               ((northM - viewport.centreNorthM) * invScale);
    };

    const size_t count = track_->size();

    // Main track line.
    for (size_t i = 0; i + 1 < count; ++i) {
        const TrackPoint& a = track_->at(i);
        const TrackPoint& b = track_->at(i + 1);

        const float x0 = toScreenX(a.eastM);
        const float y0 = toScreenY(a.northM);
        const float x1 = toScreenX(b.eastM);
        const float y1 = toScreenY(b.northM);

        drawClippedLine(display, x0, y0, x1, y1);

        /*
         * Emphasise the most recent segments by drawing them a second
         * time with a one-pixel vertical offset. On a 1-bit display this
         * is the cheapest way to get a visibly heavier line, and it
         * makes the head of the track stand out from earlier passes
         * through the same airspace.
         */
        if ((count - i) <= kHeadSegments) {
            drawClippedLine(display, x0, y0 + 1.0f, x1, y1 + 1.0f);
        }
    }

    /*
     * Takeoff / LZ sits at the projection origin. Clamped rather than
     * hidden when off-screen: knowing roughly which way the LZ lies
     * matters more than pixel-accuracy once it is out of frame.
     */
    const int16_t lzX = clampInt16(static_cast<int32_t>(toScreenX(0.0f)),
                                   kPlotX + 5, kPlotRight - 5);
    const int16_t lzY = clampInt16(static_cast<int32_t>(toScreenY(0.0f)),
                                   kPlotY + 5, kPlotBottom - 5);
    display_helpers::drawLzMarker(display, lzX, lzY);

    const int16_t acX = clampInt16(static_cast<int32_t>(toScreenX(liveEast)),
                                   kPlotX + 7, kPlotRight - 7);
    const int16_t acY = clampInt16(static_cast<int32_t>(toScreenY(liveNorth)),
                                   kPlotY + 7, kPlotBottom - 7);

    drawAircraft(display, acX, acY, data.track, deadReckoned);

    if (deadReckoned) {
        // Explicit label rather than relying on the marker style alone
        // to be noticed -- losing GPS is exactly the moment a pilot's
        // attention is likely to be elsewhere.
        display.display().setCursor(kPlotX + 2, kPlotY + 2);
        display.display().print("DR");
    }

    drawNorthIndicator(display);
    drawScaleBar(display, viewport);
}

void FlightMapScreen::drawAircraft(DisplayManager& display,
                                   int16_t x, int16_t y,
                                   float headingDeg,
                                   bool deadReckoned) const {
    /*
     * Filled arrowhead pointing along the GPS course, or an outline-only
     * arrowhead (no centre dot) when the position is a dead-reckoned
     * estimate rather than a live fix -- a pilot glancing at a solid
     * marker should be able to trust it is real without reading the "DR"
     * label at all.
     *
     * Screen bearing: 0 deg is north (up), increasing clockwise, so the
     * vertical component is negated relative to the maths convention.
     */
    const float rad = headingDeg * 0.017453292519943295f;
    const float sinH = sinf(rad);
    const float cosH = cosf(rad);

    constexpr float kNoseLen = 6.0f;
    constexpr float kTailLen = 4.0f;
    constexpr float kHalfWidth = 3.5f;

    const float noseX = static_cast<float>(x) + (sinH * kNoseLen);
    const float noseY = static_cast<float>(y) - (cosH * kNoseLen);

    // Rear corners, offset perpendicular to the heading.
    const float baseX = static_cast<float>(x) - (sinH * kTailLen);
    const float baseY = static_cast<float>(y) + (cosH * kTailLen);

    const float leftX = baseX - (cosH * kHalfWidth);
    const float leftY = baseY - (sinH * kHalfWidth);
    const float rightX = baseX + (cosH * kHalfWidth);
    const float rightY = baseY + (sinH * kHalfWidth);

    const int16_t nx = static_cast<int16_t>(noseX);
    const int16_t ny = static_cast<int16_t>(noseY);
    const int16_t lx = static_cast<int16_t>(leftX);
    const int16_t ly = static_cast<int16_t>(leftY);
    const int16_t rx = static_cast<int16_t>(rightX);
    const int16_t ry = static_cast<int16_t>(rightY);

    display.display().drawLine(nx, ny, lx, ly, SH110X_WHITE);
    display.display().drawLine(nx, ny, rx, ry, SH110X_WHITE);

    if (deadReckoned) {
        // Base of the arrow left open: an estimated position is
        // deliberately drawn as a less complete shape than a real one.
        return;
    }

    display.display().drawLine(lx, ly, rx, ry, SH110X_WHITE);

    // Solid centre so the marker reads as the current position even when
    // it overlaps the track line.
    display.display().fillCircle(x, y, 1, SH110X_WHITE);
}

void FlightMapScreen::drawNorthIndicator(DisplayManager& display) const {
    // Top-right of the plot, clear of the track in most flights.
    constexpr int16_t kX = kPlotRight - 6;
    constexpr int16_t kY = kPlotY + 3;

    display.display().drawLine(kX, kY + 8, kX, kY, SH110X_WHITE);
    display.display().drawLine(kX, kY, kX - 2, kY + 3, SH110X_WHITE);
    display.display().drawLine(kX, kY, kX + 2, kY + 3, SH110X_WHITE);

    display.display().setCursor(kX - 3, kY + 10);
    display.display().print("N");
}

void FlightMapScreen::drawScaleBar(DisplayManager& display,
                                   const Viewport& viewport) const {
    /*
     * Pick a round distance that fits in roughly a third of the width,
     * then draw the bar at whatever pixel length that distance actually
     * occupies -- so the bar is always exactly the labelled length.
     */
    const float targetM = viewport.metresPerPixel * (kPlotW / 3.0f);
    const float niceM = niceScaleLength(targetM);
    const int16_t barPx =
        static_cast<int16_t>(niceM / viewport.metresPerPixel);

    if (barPx < 4 || barPx > kPlotW) {
        return;
    }

    const int16_t barY = kPlotBottom - 2;
    const int16_t barX = kPlotX + 3;

    display.display().drawLine(barX, barY, barX + barPx, barY, SH110X_WHITE);
    display.display().drawLine(barX, barY - 2, barX, barY, SH110X_WHITE);
    display.display().drawLine(barX + barPx, barY - 2,
                               barX + barPx, barY, SH110X_WHITE);

    char label[12];
    if (niceM >= 1000.0f) {
        snprintf(label, sizeof(label), "%.0fkm", niceM / 1000.0f);
    } else {
        snprintf(label, sizeof(label), "%.0fm", niceM);
    }

    display.display().setCursor(barX + barPx + 3, barY - 6);
    display.display().print(label);
}

void FlightMapScreen::drawThreeD(DisplayManager& display,
                                 const FlightData& data) {
    (void)data;

    const size_t count = track_->size();

    /*
     * How much of the track to draw. The replay walks forward through
     * flight time at replaySpeed_ times real time, so the flight is
     * re-flown rather than presented finished.
     */
    size_t visible = count;

    if (replayRunning_) {
        const uint16_t totalSec = track_->durationSeconds();
        const uint32_t elapsedMs = millis() - replayStartMs_;
        const float replaySec =
            (static_cast<float>(elapsedMs) / 1000.0f) *
            static_cast<float>(replaySpeed_);

        if (totalSec == 0 || replaySec >= static_cast<float>(totalSec)) {
            // Hold the finished flight briefly, then loop.
            if (replaySec >= static_cast<float>(totalSec) + 2.0f) {
                replayStartMs_ = millis();
            }
            visible = count;
        } else {
            // Track points are time-ordered, so a linear scan is fine
            // and avoids assuming a uniform sample interval.
            visible = 1;
            for (size_t i = 0; i < count; ++i) {
                if (static_cast<float>(track_->at(i).timeSec) <= replaySec) {
                    visible = i + 1;
                } else {
                    break;
                }
            }
        }
    }

    if (visible < 2) {
        visible = 2;
    }

    /*
     * Isometric-style projection.
     *
     * The horizontal plane is rotated by azimuthDeg_ then squashed
     * vertically (kTilt) to suggest a viewing angle, and altitude is
     * added straight up the screen. This is cheap -- two trig calls for
     * the whole frame -- and reads clearly on a 1-bit display, where a
     * true perspective divide would gain nothing.
     */
    const float azRad = azimuthDeg_ * 0.017453292519943295f;
    const float sinA = sinf(azRad);
    const float cosA = cosf(azRad);

    constexpr float kTilt = 0.45f;

    const float spanE =
        static_cast<float>(track_->maxEast() - track_->minEast());
    const float spanN =
        static_cast<float>(track_->maxNorth() - track_->minNorth());
    const float spanH = fmaxf(spanE, fmaxf(spanN, kMinSpanMetres));

    // Leave room for the altitude column, which extends upward.
    const float horizScale =
        (static_cast<float>(kPlotW) * 0.62f) / spanH;

    const float altSpan =
        static_cast<float>(track_->maxAltitude() - track_->minAltitude());
    const float vertScale =
        (altSpan > 1.0f)
            ? ((static_cast<float>(kPlotH) * 0.42f) / altSpan)
            : 0.0f;

    const float midE =
        static_cast<float>(track_->maxEast() + track_->minEast()) * 0.5f;
    const float midN =
        static_cast<float>(track_->maxNorth() + track_->minNorth()) * 0.5f;
    const float baseAlt = static_cast<float>(track_->minAltitude());

    const int16_t originY = kPlotBottom - 12;

    const auto projectX = [&](float eastM, float northM) -> float {
        const float e = eastM - midE;
        const float n = northM - midN;
        return static_cast<float>(kPlotCentreX) +
               (((e * cosA) - (n * sinA)) * horizScale);
    };

    const auto projectY = [&](float eastM, float northM, float altM) -> float {
        const float e = eastM - midE;
        const float n = northM - midN;
        const float ground = ((e * sinA) + (n * cosA)) * horizScale * kTilt;
        const float height = (altM - baseAlt) * vertScale;
        return static_cast<float>(originY) + ground - height;
    };

    // Ground reference at the LZ, so height above takeoff is readable.
    const int16_t lzGroundX =
        clampInt16(static_cast<int32_t>(projectX(0.0f, 0.0f)),
                   kPlotX + 5, kPlotRight - 5);
    const int16_t lzGroundY =
        clampInt16(static_cast<int32_t>(projectY(0.0f, 0.0f, baseAlt)),
                   kPlotY + 5, kPlotBottom - 5);
    display_helpers::drawLzMarker(display, lzGroundX, lzGroundY);

    for (size_t i = 0; i + 1 < visible; ++i) {
        const TrackPoint& a = track_->at(i);
        const TrackPoint& b = track_->at(i + 1);

        drawClippedLine(display,
                        projectX(a.eastM, a.northM),
                        projectY(a.eastM, a.northM, a.altitudeM),
                        projectX(b.eastM, b.northM),
                        projectY(b.eastM, b.northM, b.altitudeM));
    }

    // Mark the replay head with a dropline to the ground plane, which is
    // what makes the height readable in a projection with no depth cues.
    const TrackPoint& head = track_->at(visible - 1);

    const int16_t headX =
        clampInt16(static_cast<int32_t>(projectX(head.eastM, head.northM)),
                   kPlotX + 2, kPlotRight - 2);
    const int16_t headY =
        clampInt16(static_cast<int32_t>(projectY(head.eastM, head.northM, head.altitudeM)),
                   kPlotY + 2, kPlotBottom - 2);
    const int16_t groundY =
        clampInt16(static_cast<int32_t>(projectY(head.eastM, head.northM, baseAlt)),
                   kPlotY + 2, kPlotBottom - 2);

    for (int16_t y = headY; y < groundY; y += 3) {
        display.display().drawPixel(headX, y, SH110X_WHITE);
    }

    display.display().fillCircle(headX, headY, 2, SH110X_WHITE);

    // Orientation and replay state.
    char label[24];
    snprintf(label, sizeof(label), "%03d deg  %ux",
             static_cast<int>(azimuthDeg_),
             static_cast<unsigned>(replaySpeed_));

    display.display().setCursor(kPlotX + 2, kPlotBottom - 6);
    display.display().print(label);

    snprintf(label, sizeof(label), "+%dm",
             static_cast<int>(head.altitudeM - track_->minAltitude()));
    display.display().setCursor(kPlotRight - 30, kPlotY + 2);
    display.display().print(label);
}

}  // namespace variometer
