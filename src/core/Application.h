#pragma once

#include "core/FlightData.h"
#include "sensors/GPS.h"
#include "sensors/MS5611Sensor.h"
#include "input/Encoder.h"
#include "power/BatteryMonitor.h"
#include "power/PowerManager.h"
#include "audio/Buzzer.h"
#include "flight/FlightDetector.h"
#include "flight/VarioCalculator.h"
#include "flight/WindEstimator.h"
#include "flight/FlightRecorder.h"
#include "flight/FlightTrack.h"
#include "flight/FlightLogStorage.h"
#include "display/DisplayManager.h"

namespace variometer {

class Application {
public:
    Application();
    void begin();
    void loop();

private:
    void updateSensors();
    void updateFlightLogic();
    void updateAudio();
    void updateDisplay();
    void initializeFlightSession();
    float calculateDistanceFromLz(float latitude, float longitude) const;

    FlightData flightData_;
    DeviceSettings settings_;
    // NOTE: there is deliberately no lastGpsMs_ -- the GPS UART is
    // drained every loop pass. See Application::updateSensors().
    uint32_t lastMs5611Ms_ = 0;
    uint32_t lastBatteryMs_ = 0;
    uint32_t lastLogicMs_ = 0;
    uint32_t lastAudioMs_ = 0;
    uint32_t lastDisplayMs_ = 0;
    uint32_t lastTraceSampleMs_ = 0;
    uint32_t lastTrackSampleMs_ = 0;
    uint32_t flightStartTimeMs_ = 0;
    GPS gps_;
    MS5611Sensor ms5611_;
    Encoder encoder_;
    BatteryMonitor batteryMonitor_;
    PowerManager powerManager_;
    Buzzer buzzer_;
    FlightDetector flightDetector_;
    VarioCalculator varioCalculator_;
    WindEstimator windEstimator_;
    FlightRecorder flightRecorder_;
    FlightTrack flightTrack_;
    FlightLogStorage flightLogStorage_;
    DisplayManager display_;

    bool mockAltitudeInitialized = false;
    bool lastMockMode = false;
    float mockAltitudeBase = 0.0f;
    float lastMockAltitude = 0.0f;
    uint32_t lastMockAltitudeMs = 0;

    /*
     * Altitude sample handshake handed to VarioCalculator.
     *
     * Incremented once per genuinely new altitude reading, whatever the
     * active source (barometer or mock GPS feed). Keeping this in
     * Application means VarioCalculator does not need to know which
     * source is live, and the vario's sample rate follows the sensor
     * rather than the loop rate.
     */
    uint32_t altitudeSampleSeq_ = 0;
    uint32_t altitudeSampleTimeMs_ = 0;

    // Last MS5611 sequence seen, used to detect a fresh barometer sample.
    uint32_t lastMs5611Seq_ = 0;

#ifdef ALTITUDE_SOURCE_GPS
    // Only used when altitude is sourced from GPS; see updateSensors().
    uint32_t lastGpsAltitudeSeq_ = 0;
    float gpsAltitudeReference_ = 0.0f;
    bool gpsAltitudeReferenceSet_ = false;
#endif

    /*
     * Health / timing instrumentation.
     *
     * The whole diagnosis of the erratic vario rested on an assumption
     * about how long a loop pass actually takes. These measure it rather
     * than assume it: if loopRate is far below the ~50 Hz the timing
     * gates expect, something is still blocking the loop.
     */
    void reportHealth();
    void noteAltitudeSample(float altitude);

    uint32_t lastHealthMs_ = 0;
    uint32_t loopCount_ = 0;
    uint32_t loopMaxUs_ = 0;
    uint32_t loopSumUs_ = 0;
    uint32_t displayCount_ = 0;
    uint32_t displayMaxUs_ = 0;
    uint32_t displaySumUs_ = 0;
    uint32_t altitudeSampleCount_ = 0;

    /*
     * Altitude spread over the reporting interval.
     *
     * With the device stationary this is a direct measurement of sensor
     * noise, and it settles the main open question cheaply: an MS5611 at
     * OSR 4096 should hold roughly 0.1-0.3 m peak-to-peak. Metres of
     * spread means the problem is upstream of the vario maths, and no
     * amount of filter tuning will fix it.
     */
    float altitudeMin_ = 0.0f;
    float altitudeMax_ = 0.0f;
    bool altitudeSpreadValid_ = false;

    // Scans the shared bus once at startup and lists responding
    // addresses, so a missing or intermittent device is obvious.
    void scanI2cBus();
};

}  // namespace variometer

