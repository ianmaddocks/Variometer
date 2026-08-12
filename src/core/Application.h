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
    uint32_t lastSensorMs_ = 0;
    uint32_t lastLogicMs_ = 0;
    uint32_t lastAudioMs_ = 0;
    uint32_t lastDisplayMs_ = 0;
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
    DisplayManager display_;
};

}  // namespace variometer
