#include "core/Application.h"

#include <Arduino.h>
#include <math.h>
#include <Wire.h>

#include "config/Config.h"
#include "utils/GeoUtils.h"

namespace variometer {

Application::Application()
    : gps_(),
    biometricSensor_(),
      encoder_(),
      batteryMonitor_(),
      buzzer_(),
      flightDetector_(),
      varioCalculator_(),
      windEstimator_(),
      display_() {}

void Application::begin() {
    Serial.begin(115200);
    Wire.begin(Config::I2C_SDA, Config::I2C_SCL);

    /*
     * Must be set explicitly -- the Arduino core leaves Wire at 100 kHz,
     * where a single full 128x128 OLED refresh takes ~184 ms and starves
     * every other subsystem in loop(). See Config::I2C_CLOCK_HZ.
     */
    // Sensors are the default; the display raises the clock briefly for
    // its own flush and restores this afterwards.
    Wire.setClock(Config::I2C_CLOCK_SENSORS_HZ);

    DBGF("I2C configured: sensors %lu Hz, display %lu Hz\n",
         static_cast<unsigned long>(Config::I2C_CLOCK_SENSORS_HZ),
         static_cast<unsigned long>(Config::I2C_CLOCK_DISPLAY_HZ));

    // Enumerate the bus before any driver touches it, so a device that
    // is absent or marginal at this clock speed shows up immediately.
    scanI2cBus();

    gps_.begin();
    biometricSensor_.begin();
    encoder_.begin();
    sw1Button_.begin(Config::SW1_PIN, "SW1");
    sw2Button_.begin(Config::SW2_PIN, "SW2");
    batteryMonitor_.begin();
    powerManager_.begin();
    buzzer_.begin();
#ifdef NDEBUG
    flightLogStorage_.begin();
#endif
    display_.begin();
    display_.setPowerManager(&powerManager_);
    display_.setRecorder(&flightRecorder_);
    display_.setTrack(&flightTrack_);
    display_.setReplaySpeed(settings_.replaySpeed);
    settings_.minSatellites = Config::MIN_SATELLITES_DEFAULT;

    // Buzzer defaults to disabled inside the driver; without this it never sounds.
    buzzer_.setEnabled(settings_.audioVarioEnabled);
    buzzer_.playStartupTune();

    // Haptic motor shares D0 with the (now disabled) buzzer -- see
    // Buzzer.cpp -- and is the only ascent/descent feedback actually
    // wired up on this board.
    haptic_.begin();
    haptic_.setEnabled(settings_.hapticVarioEnabled);

    // BLE stack init is independent of the I2C bus and sensors above, so
    // it does not need to sit before or after any of that setup.
    bleTelemetry_.begin();
}

void Application::scanI2cBus() {
    uint8_t found = 0;

    DBGLN("I2C scan:");

    /*
     * Starts at 0x01, not the usual 0x08. Addresses below 0x08 are
     * reserved by the I2C spec, but the DuPPa encoder is configured to
     * one of them here (Config::ENCODER_I2C_ADDRESS), so a scan over the
     * conventional range would report it missing when it is present.
     */
    for (uint8_t address = 0x01; address <= 0x77; ++address) {
        Wire.beginTransmission(address);

        if (Wire.endTransmission() == 0) {
            const char* name = "unknown";

            if (address == 0x76 || address == 0x77) {
                name = "biometricSensor";
            } else if (address == 0x3C || address == 0x3D) {
                name = "SH1107 OLED";
            } else if (address == Config::ENCODER_I2C_ADDRESS) {
                name = "DuPPa encoder";
            }

            DBGF("  0x%02X  %s\n", address, name);
            ++found;
        }
    }

    DBGF("I2C scan complete, %u device(s) responding\n", found);
}

void Application::noteAltitudeSample(float altitude) {
    ++altitudeSampleCount_;

    if (!altitudeSpreadValid_) {
        altitudeMin_ = altitude;
        altitudeMax_ = altitude;
        altitudeSpreadValid_ = true;
        return;
    }

    if (altitude < altitudeMin_) {
        altitudeMin_ = altitude;
    }

    if (altitude > altitudeMax_) {
        altitudeMax_ = altitude;
    }
}

void Application::reportHealth() {
    const BiometricSensor::Counters& baro = biometricSensor_.getCounters();

    const uint32_t elapsedMs =
        (lastHealthMs_ == 0) ? Config::HEALTH_REPORT_INTERVAL_MS
                             : (millis() - lastHealthMs_);

    const float seconds =
        (elapsedMs > 0) ? (static_cast<float>(elapsedMs) / 1000.0f) : 1.0f;

    const uint32_t loopAvgUs =
        (loopCount_ > 0) ? (loopSumUs_ / loopCount_) : 0;

    const uint32_t displayAvgUs =
        (displayCount_ > 0) ? (displaySumUs_ / displayCount_) : 0;

    /*
     * One line, everything needed to tell the three candidate faults
     * apart:
     *   loop  -- is anything still blocking the main loop?
     *   disp  -- is the display flush the thing blocking it?
     *   baro  -- how many good samples vs each distinct failure mode
     *   alt   -- what rate is the vario actually being fed at
     */
    DBGF("HEALTH: loop=%luHz avg=%luus max=%luus | disp=%luHz avg=%luus max=%luus | "
         "baro=%.1f/s ok=%lu pFail=%lu tFail=%lu cmdFail=%lu rdFail=%lu rng=%lu tRej=%lu "
         "i2cErr=%u crc=%s | alt=%.1f/s spread=%.2fm vario=%.2fm/s heap=%lu\n",
         static_cast<unsigned long>(loopCount_ / (seconds > 0 ? seconds : 1)),
         static_cast<unsigned long>(loopAvgUs),
         static_cast<unsigned long>(loopMaxUs_),
         static_cast<unsigned long>(displayCount_ / (seconds > 0 ? seconds : 1)),
         static_cast<unsigned long>(displayAvgUs),
         static_cast<unsigned long>(displayMaxUs_),
         static_cast<double>(baro.samples) / seconds,
         static_cast<unsigned long>(baro.samples),
         static_cast<unsigned long>(baro.adcFailPressure),
         static_cast<unsigned long>(baro.adcFailTemp),
         static_cast<unsigned long>(baro.convertFail),
         static_cast<unsigned long>(baro.readFail),
         static_cast<unsigned long>(baro.rangeReject),
         static_cast<unsigned long>(baro.tempReject),
         biometricSensor_.getLastI2cError(),
         biometricSensor_.hasValidCalibration() ? "OK" : "BAD",
         static_cast<double>(altitudeSampleCount_) / seconds,
         static_cast<double>(altitudeSpreadValid_ ? (altitudeMax_ - altitudeMin_) : 0.0f),
         flightData_.verticalSpeed,
         static_cast<unsigned long>(ESP.getFreeHeap()));

    // Reset for the next interval so every line reports a rate.
    biometricSensor_.resetCounters();
    loopCount_ = 0;
    loopMaxUs_ = 0;
    loopSumUs_ = 0;
    displayCount_ = 0;
    displayMaxUs_ = 0;
    displaySumUs_ = 0;
    altitudeSampleCount_ = 0;
    altitudeSpreadValid_ = false;
    lastHealthMs_ = millis();
}

void Application::loop() {
    const uint32_t loopStartUs = micros();
    const uint32_t now = millis();

#ifdef NDEBUG
    flightLogStorage_.update();
#endif

    encoder_.update();
    sw1Button_.update();
    sw2Button_.update();

    // SW1/SW2 get the same haptic acknowledgement as the encoder press
    // below, independent of whatever action (if any) the press below
    // ends up triggering.
    if (sw1Button_.wasPressed() || sw2Button_.wasPressed()) {
        haptic_.triggerPulse(Config::HAPTIC_BUTTON_PULSE_MS);
    }

    if (encoder_.wasPressed()) {
        haptic_.triggerPulse(Config::HAPTIC_BUTTON_PULSE_MS);
        display_.handleButtonPress();

        /*
         * Gated on the Pre-Takeoff screen specifically, not merely on
         * PREFLIGHT state. The spec ties manual takeoff to "pressing the
         * encoder button while in this screen" (Pre-Takeoff); the check
         * used to be state-only, so a press on ANY preflight screen --
         * StartUp while still waiting for GPS lock, Settings, even
         * PowerOff -- silently forced takeoff. On Settings specifically
         * that meant a single press both toggled edit mode AND started
         * the flight, since handleButtonPress() above reads the same
         * press for its own screen-specific action.
         */
        if (flightData_.flightState == FlightState::PREFLIGHT &&
            display_.isPreTakeoffScreen()) {
            flightDetector_.requestTakeoff();
        }
    }

    /*
     * SW2: dedicated hardware takeoff button, an alternative to the
     * GPS-speed-based detection in FlightDetector::update() and to the
     * encoder press above. Not tied to any particular screen -- it is a
     * physical switch, not a UI gesture -- so it only needs to check
     * flight state.
     */
    if (sw2Button_.wasPressed() && flightData_.flightState == FlightState::PREFLIGHT) {
        DBGLN("Takeoff requested via SW2");
        flightDetector_.requestTakeoff();
    }

    // SW1: hardware stand-in for the encoder's double-push (DPUSH), so
    // every consumeDoublePress()/wasDoublePressed() site below also
    // fires on a SW1 press.
    const bool doublePressEvent = encoder_.consumeDoublePress() || sw1Button_.wasPressed();

    if (display_.isPowerOffScreen() && doublePressEvent) {
        powerManager_.requestPowerOff();
        buzzer_.playPowerOffTune();
        DBGLN("Power-off sequence initiated by double press on Power Off screen");
    }

#ifdef DEBUG
    // Debug/test-only: double-press anywhere swaps in a scripted GPS feed.
    // Deliberately excluded from release builds so it can't be triggered
    // by an accidental double-press in flight.
    if (encoder_.wasDoublePressed() || sw1Button_.wasPressed()) {
        gps_.enableMockFeed();
    }
#endif


    if (encoder_.getDelta() != 0) {
        display_.handleEncoderDelta(encoder_.getDelta());
    }

    updateSensors();

    if (now - lastLogicMs_ >= 50) {
        updateFlightLogic();
        lastLogicMs_ = now;
    }

    /*
     * Safe to call every pass: BleTelemetry internally rate-limits its
     * own sentence timers and returns immediately when nothing is
     * connected, so this costs nothing extra on top of the check it was
     * already going to do.
     */
    bleTelemetry_.update(flightData_, gps_.getUtcDateTime(), biometricSensor_.isValid(),
                         biometricSensor_.getPressure(), biometricSensor_.getTemperature());

    if (now - lastAudioMs_ >= 20) {
        updateAudio();
        lastAudioMs_ = now;
    }

    if (now - lastDisplayMs_ >= Config::DISPLAY_UPDATE_INTERVAL_MS) {
        // Timed separately: the full 128x128 flush is the single
        // biggest block of I2C traffic in the system and the prime
        // suspect whenever the loop rate collapses.
        const uint32_t displayStartUs = micros();

        /*
         * The OLED is the one device that needs a fast bus, and it is
         * tolerant of it. Raise the clock for the flush only, then hand
         * the bus back to the sensors at their safe speed.
         */
        Wire.setClock(Config::I2C_CLOCK_DISPLAY_HZ);

        updateDisplay();

        Wire.setClock(Config::I2C_CLOCK_SENSORS_HZ);

        const uint32_t displayUs = micros() - displayStartUs;

        displaySumUs_ += displayUs;
        ++displayCount_;

        if (displayUs > displayMaxUs_) {
            displayMaxUs_ = displayUs;
        }

        lastDisplayMs_ = now;
    }

    if (powerManager_.shouldPowerOff()) {
        DBGLN("Power-off requested");
    }

    /*
     * Loop timing is sampled last so it covers the whole pass. max is
     * more diagnostic than avg here -- a single long stall is what
     * starves the sensor state machine, and it disappears into an
     * average.
     */
    const uint32_t loopUs = micros() - loopStartUs;

    loopSumUs_ += loopUs;
    ++loopCount_;

    if (loopUs > loopMaxUs_) {
        loopMaxUs_ = loopUs;
    }

    if (now - lastHealthMs_ >= Config::HEALTH_REPORT_INTERVAL_MS) {
        reportHealth();
    }
}

void Application::updateSensors() {
    const uint32_t now = millis();

    /*
     * Drain the GPS UART every pass, not on a timer.
     *
     * At 115200 baud the receiver emits ~11.5 kB/second, so the old
     * 1000 ms polling interval overflowed the RX buffer many times over
     * between reads and delivered truncated NMEA. gps_.update() returns
     * immediately when nothing is waiting, so this is cheap.
     */
    gps_.update();

    if (now - lastBiometricSensorMs_ >= Config::BIOMETRIC_SENSOR_UPDATE_INTERVAL_MS) {
        biometricSensor_.update();
        lastBiometricSensorMs_ = now;
    }

    if (now - lastBatteryMs_ >= Config::BATTERY_UPDATE_INTERVAL_MS) {
        batteryMonitor_.update();
        lastBatteryMs_ = now;
    }

    if (gps_.hasData()) {
        flightData_.latitude = gps_.getLatitude();
        flightData_.longitude = gps_.getLongitude();
        flightData_.gpsAltitude = gps_.getAltitude();
        flightData_.groundSpeed = gps_.getGroundSpeed();
        flightData_.track = gps_.getTrack();
        flightData_.satellites = gps_.getSatellites();
        flightData_.gpsFix = gps_.getFixStatus();
    }
    if (flightData_.flightState == FlightState::TAKEOFF_DETECTED ||
        flightData_.flightState == FlightState::FLIGHT ||
        flightData_.flightState == FlightState::LANDING_DETECTED ||
        flightData_.flightState == FlightState::POST_FLIGHT) {
        if (flightStartTimeMs_ > 0) {
            flightData_.flightDuration = (millis() - flightStartTimeMs_) / 1000;
        }
    } else {
        flightData_.flightDuration = 0;
    }

    if (flightData_.hasLz && flightData_.gpsFix) {
        flightData_.distanceFromLZ = calculateDistanceFromLz(flightData_.latitude, flightData_.longitude);
        flightData_.bearingToLZ = geo::bearingDegrees(flightData_.latitude, flightData_.longitude,
                                                       flightData_.lzLatitude, flightData_.lzLongitude);
    } else {
        flightData_.distanceFromLZ = 0.0f;
        flightData_.bearingToLZ = 0.0f;
    }

    if (gps_.isMockEnabled() != lastMockMode) {
        lastMockMode = gps_.isMockEnabled();
        mockAltitudeInitialized = false;
        mockAltitudeBase = 0.0f;
        lastMockAltitude = 0.0f;
        lastMockAltitudeMs = 0;
        DBGLN("Mock Mode enabled");
    }

    if (gps_.isMockEnabled()) {
        if (!mockAltitudeInitialized) {
            mockAltitudeBase = gps_.getAltitude();
            lastMockAltitude = gps_.getAltitude();
            lastMockAltitudeMs = now;
            mockAltitudeInitialized = true;
            DBGLN("Mock Alt enabled");
        }


        if ((now - lastMockAltitudeMs) >= 500) {  // Update derived vario
            const float currentAltitude = gps_.getAltitude();
            flightData_.barometricAltitude = currentAltitude;
            flightData_.relativeAltitude = currentAltitude - mockAltitudeBase;

            /*
            const float dt = (now - lastMockAltitudeMs) / 1000.0f;  // Convert milliseconds to seconds
            if (dt > 0.0f) {
                float derivedVario = (currentAltitude - lastMockAltitude) / dt;
                if (derivedVario > 5.0f) {
                    derivedVario = 5.0f;
                } else if (derivedVario < -5.0f) {
                    derivedVario = -5.0f;
                }
                flightData_.verticalSpeed = derivedVario;
            }
            */
            flightData_.verticalSpeed = 0.0f;
            lastMockAltitude = currentAltitude;
            lastMockAltitudeMs = now;

            // Mock feed is the altitude source here, so it advertises
            // the new sample just as the barometer would.
            altitudeSampleTimeMs_ = now;
            ++altitudeSampleSeq_;
            noteAltitudeSample(flightData_.barometricAltitude);
     }


#ifdef ALTITUDE_SOURCE_GPS
    /*
     * Altitude/vario sourced from the GPS's own GGA fix instead of the
     * barometer.
     *
     * Selected at compile time via Config::ALTITUDE_SOURCE_GPS (see
     * platformio.ini) so the rest of the flight logic -- vario filter,
     * flight detection, the map, the altitude trace -- can be exercised
     * without a working barometer. This is a bench/debug aid, not a
     * flight configuration: GPS altitude is materially noisier and
     * slower than a barometer (see the warning at Config.h), and this
     * path is only ever compiled in deliberately, never chosen at
     * runtime, so there is no risk of silently flying on it.
     */
    } else if (gps_.hasData()) {
        // Mirrors the biometric sensor's own behaviour: establish a reference on
        // the first valid reading so relativeAltitude reads sensibly
        // during preflight, before setTakeoffReference()'s equivalent
        // below overwrites it at takeoff.
        if (!gpsAltitudeReferenceSet_) {
            gpsAltitudeReference_ = gps_.getAltitude();
            gpsAltitudeReferenceSet_ = true;
        }

        flightData_.barometricAltitude = gps_.getAltitude();
        flightData_.relativeAltitude =
            gps_.getAltitude() - gpsAltitudeReference_;

        // Same handshake pattern as the biometric sensor path below: only feed the
        // vario a sample when a new GGA fix has actually arrived.
        const uint32_t gpsSeq = gps_.getAltitudeSampleSequence();

        if (gpsSeq != lastGpsAltitudeSeq_) {
            lastGpsAltitudeSeq_ = gpsSeq;
            altitudeSampleTimeMs_ = gps_.getAltitudeSampleTimeMs();
            ++altitudeSampleSeq_;
            noteAltitudeSample(flightData_.barometricAltitude);
        }
#else
    } else if (biometricSensor_.isValid()) {
        flightData_.barometricAltitude = biometricSensor_.getAltitude();
        flightData_.relativeAltitude = biometricSensor_.getRelativeAltitude();

        /*
         * Forward the barometer's own sample handshake to the vario.
         *
         * We deliberately publish the sensor's measurement timestamp
         * rather than `now`: the reading may have completed up to one
         * poll interval ago, and feeding the readout time into the
         * regression added that error to every sample.
         */
        const uint32_t sensorSeq = biometricSensor_.getSampleSequence();

        if (sensorSeq != lastBiometricSensorSeq_) {
            lastBiometricSensorSeq_ = sensorSeq;
            altitudeSampleTimeMs_ = biometricSensor_.getSampleTimeMs();
            ++altitudeSampleSeq_;
            noteAltitudeSample(flightData_.barometricAltitude);
        }
#endif
    }

    if ((flightData_.flightState == FlightState::FLIGHT ||
         flightData_.flightState == FlightState::TAKEOFF_DETECTED) &&
        now - lastTraceSampleMs_ >= Config::ALTITUDE_TRACE_SAMPLE_INTERVAL_MS) {
        const bool wasEmpty = (flightRecorder_.size() == 0);

        flightRecorder_.addPoint(flightData_.barometricAltitude,
                                 static_cast<float>(now) / 1000.0f,
                                 flightData_.latitude,
                                 flightData_.longitude);
        flightLogStorage_.appendPoint(flightRecorder_.at(flightRecorder_.size() - 1));
        lastTraceSampleMs_ = now;

        /*
         * Update the running min/max incrementally instead of rescanning
         * the whole recorder (see below). A new point arrives at most
         * once per ALTITUDE_TRACE_SAMPLE_INTERVAL_MS, so this is the only
         * place the range can actually change.
         */
        const float newAltitude = flightData_.barometricAltitude;

        if (wasEmpty) {
            flightData_.traceAltitudeMin = newAltitude;
            flightData_.traceAltitudeMax = newAltitude;
        } else {
            if (newAltitude < flightData_.traceAltitudeMin) {
                flightData_.traceAltitudeMin = newAltitude;
            }
            if (newAltitude > flightData_.traceAltitudeMax) {
                flightData_.traceAltitudeMax = newAltitude;
            }
        }
        flightData_.traceAltitudeSpan = flightData_.traceAltitudeMax - flightData_.traceAltitudeMin;
    }

    /*
     * Offer positions to the map track.
     *
     * Polled on a short timer rather than every pass because projecting
     * and range-checking a sample costs a square root, and the track's
     * own distance filter decides what is actually worth storing. The
     * map redraws from whatever the track holds, so this rate is
     * independent of the display's.
     */
    if ((flightData_.flightState == FlightState::FLIGHT ||
         flightData_.flightState == FlightState::TAKEOFF_DETECTED) &&
        now - lastTrackSampleMs_ >= Config::FLIGHT_TRACK_SAMPLE_INTERVAL_MS) {
        flightTrack_.addSample(flightData_);
        lastTrackSampleMs_ = now;
    }

    /*
     * traceAltitudeMin/Max/Span are maintained incrementally above, at
     * the point a new sample is actually added -- not here.
     *
     * This used to rescan the entire recorder (up to MAX_POINTS =
     * FLIGHT_RECORDING_DURATION_MINUTES * 60, i.e. 7200 points for a
     * full 2-hour flight) unconditionally on every loop() pass, even
     * though a new point arrives at most once a second. At the loop
     * rates this app runs (several hundred Hz), that repeated the same
     * O(n) scan hundreds of times between each new point for no new
     * information -- millions of redundant comparisons per second on a
     * flight nearing the buffer's capacity.
     *
     * Known trade-off: once the recorder is full it evicts its oldest
     * point per new sample (see RingBuffer). The incremental min/max
     * above never revisits evicted points, so on a flight long enough to
     * start evicting (> FLIGHT_RECORDING_DURATION_MINUTES), a range
     * extreme that has since scrolled out of the buffer stays reported
     * until a new extreme replaces it -- whereas the old rescan would
     * have reflected only what's currently retained. Accepted because
     * the flights this device targets rarely exceed the buffer's 2-hour
     * span, and paying an O(n) scan every loop to handle the rare case
     * that does is the wrong trade the rest of the time.
     */
    flightData_.tracePointCount = static_cast<uint16_t>(flightRecorder_.size());
    if (flightRecorder_.size() == 0) {
        flightData_.traceAltitudeMin = flightData_.barometricAltitude;
        flightData_.traceAltitudeMax = flightData_.barometricAltitude;
        flightData_.traceAltitudeSpan = 0.0f;
    }

    flightData_.batteryVoltage = batteryMonitor_.getVoltage();
    flightData_.batteryPercent = batteryMonitor_.getPercent();

}

void Application::updateFlightLogic() {
    const FlightState previousState = flightData_.flightState;
    flightDetector_.update(flightData_);
    const FlightState detectorState = flightDetector_.getState();
    if ((previousState == FlightState::PREFLIGHT || previousState == FlightState::POST_FLIGHT) &&
        detectorState == FlightState::TAKEOFF_DETECTED) {
        initializeFlightSession();
        buzzer_.playTakeoffTone();
    }
    if (previousState != FlightState::POST_FLIGHT && detectorState == FlightState::POST_FLIGHT) {
        flightLogStorage_.finishFlight(flightData_.flightDuration);
    }
    flightData_.flightState = detectorState;

    /*
     * Safe to call every pass: the calculator ingests a sample only when
     * altitudeSampleSeq_ changes, so it tracks the true measurement rate
     * instead of this function's polling rate.
     */
    varioCalculator_.update(flightData_, altitudeSampleSeq_, altitudeSampleTimeMs_);
    flightData_.verticalSpeed = varioCalculator_.getVerticalSpeed();
    flightData_.verticalSpeedAverage30s = varioCalculator_.getVerticalSpeedAverage30s();
    /*
     * Safe to call every pass: WindEstimator ingests a sample only when
     * GPS's position sequence changes, mirroring how the vario is fed
     * above.
     */
    windEstimator_.update(flightData_, gps_.getPositionSampleSequence());
    flightData_.windSpeed = windEstimator_.getWindSpeed();
    flightData_.windDirection = windEstimator_.getWindDirection();
    flightData_.windConfidence = windEstimator_.getWindConfidence();
}

void Application::updateAudio() {
    buzzer_.update();
    buzzer_.updateVarioFeedback(flightData_.verticalSpeed);
    haptic_.update();
    haptic_.updateVarioFeedback(flightData_.verticalSpeed);
}

void Application::initializeFlightSession() {
    if (flightData_.gpsFix) {
        flightData_.lzLatitude = flightData_.latitude;
        flightData_.lzLongitude = flightData_.longitude;
        flightData_.hasLz = true;
    }

    flightRecorder_.clear();

    /*
     * Anchor the map's projection at the takeoff point. Done here rather
     * than lazily on first draw so the LZ stays fixed for the whole
     * flight, as the map's origin and its takeoff marker.
     */
    if (flightData_.hasLz) {
        flightTrack_.begin(flightData_.lzLatitude, flightData_.lzLongitude);
    }

    // Establish zero altitude at takeoff.
#ifdef ALTITUDE_SOURCE_GPS
    gpsAltitudeReference_ = gps_.getAltitude();
#else
    biometricSensor_.setTakeoffReference();
#endif

    // Discard pre-flight altitude samples so the first
    // flight vario calculation starts cleanly.
    varioCalculator_.reset();

    if (!flightLogStorage_.startFlight(gps_.getUtcDateTime())) {
        DBGLN("Unable to start persistent flight log");
    }

    lastTraceSampleMs_ = millis() - Config::ALTITUDE_TRACE_SAMPLE_INTERVAL_MS;
    flightStartTimeMs_ = millis();
    flightData_.flightDuration = 0;
    flightData_.distanceFromLZ = 0.0f;
    flightData_.tracePointCount = 0;

    DBGF("Flight session initialized at %.5f, %.5f\n",
         flightData_.lzLatitude,
         flightData_.lzLongitude);
}

float Application::calculateDistanceFromLz(float latitude, float longitude) const {
    if (!flightData_.hasLz) {
        return 0.0f;
    }

    // Delegated to GeoUtils so the map and the distance readout cannot
    // drift apart; this was previously an inline copy of the haversine.
    return geo::distanceKm(latitude, longitude,
                           flightData_.lzLatitude, flightData_.lzLongitude);
}

void Application::updateDisplay() {
    display_.update(flightData_);
}

}  // namespace variometer
