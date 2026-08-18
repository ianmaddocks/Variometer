#pragma once

#include <stdint.h>

#include "core/FlightData.h"
#include "display/Screen.h"

namespace variometer {

class FlightTrack;

/*
 * Overhead flight map, north-up, plus a post-flight 3D replay.
 *
 * Rendering is driven entirely from FlightTrack, which is already
 * projected into LZ-relative metres. Nothing here depends on the GPS
 * update rate: the screen redraws at the display rate and simply shows
 * whatever the track currently holds.
 *
 * The world-to-screen transform is deliberately exposed as small
 * helpers (metresToScreen) so later overlays -- a wind arrow, airspace,
 * waypoints -- can plot into the same frame without duplicating the
 * projection or the zoom logic.
 */
class FlightMapScreen : public Screen {
public:
    enum class ViewMode : uint8_t {
        TopDown,
        ThreeD
    };

    FlightMapScreen() = default;

    void enter() override;
    void update(const FlightData& data) override;
    void draw(DisplayManager& display, const FlightData& data) override;
    void exit() override;

    void setTrack(const FlightTrack* track) { track_ = track; }

    /*
     * The 3D view is only meaningful once a flight is complete, so the
     * caller gates availability on flight state rather than this class
     * second-guessing it.
     */
    bool canEnterThreeD(FlightState state) const;
    void toggleViewMode(FlightState state);
    bool isThreeD() const { return viewMode_ == ViewMode::ThreeD; }

    // In 3D the encoder rotates the view instead of changing screen.
    void rotateView(int8_t delta);

    void setReplaySpeed(uint8_t speed);

private:
    struct Viewport {
        // Metres-per-pixel and the world point at the centre of the plot.
        float metresPerPixel;
        float centreEastM;
        float centreNorthM;
    };

    // liveEastM/liveNorthM is the (possibly dead-reckoned) current
    // position, computed once per frame and threaded through so the
    // follow-mode centring and the aircraft marker never disagree.
    Viewport computeViewport(float liveEastM, float liveNorthM) const;

    void drawTopDown(DisplayManager& display, const FlightData& data);
    void drawThreeD(DisplayManager& display, const FlightData& data);

    void drawNorthIndicator(DisplayManager& display) const;
    void drawScaleBar(DisplayManager& display, const Viewport& viewport) const;
    void drawTakeoffMarker(DisplayManager& display, int16_t x, int16_t y) const;
    void drawAircraft(DisplayManager& display,
                      int16_t x, int16_t y, float headingDeg,
                      bool deadReckoned) const;
    void drawEmptyState(DisplayManager& display) const;

    const FlightTrack* track_ = nullptr;

    ViewMode viewMode_ = ViewMode::TopDown;

    /*
     * Auto-zoom staging.
     *
     * Early in a flight the whole track fits comfortably and showing all
     * of it -- takeoff included -- is the most useful view. Once the
     * flight outgrows the screen, a fit-all view compresses recent
     * manoeuvring into a few pixels, so the map follows the aircraft
     * instead and periodically pulls back to re-establish context.
     */
    bool followMode_ = false;
    uint32_t lastOverviewMs_ = 0;
    bool overviewActive_ = false;
    uint32_t overviewStartedMs_ = 0;

    // 3D view state.
    float azimuthDeg_ = 30.0f;
    uint8_t replaySpeed_ = 3;
    uint32_t replayStartMs_ = 0;
    bool replayRunning_ = false;
};

}  // namespace variometer
