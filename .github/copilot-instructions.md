## Copilot Instructions — Paramotor Variometer  
## Project Overview  
Create a robust, modular C++/PlatformIO firmware application for a handheld paramotor flight computer / variometer.  
The project runs on a **Seeed Studio XIAO ESP32C3**.   
The application combines:  
* MS5611 barometric pressure sensor  
* SH1107 128x128 OLED display  
* DuPPA i2c encoder V2.1, with a tri-colour LED  
* u-blox M10 GPS receiver  
* Battery voltage monitoring  
* Buzzer driven by an NPN transistor  
* Haptic motor via NPN transistor  
The firmware must be structured as a maintainable embedded application rather than a simple Arduino sketch.  
Prioritise reliability, deterministic behaviour, low memory usage and clear separation of responsibilities.  
Do not put all functionality into main.cpp.  

⸻  
  
## Hardware  
## Microcontroller  
**Seeed Studio XIAO ESP32C3**  
Use PlatformIO and the Arduino framework unless there is a compelling reason to use another framework.  
  
⸻  
  
## Pin Assignments  
These pin assignments are fixed and must not be changed without explicit instruction.  

| Function        | Pin |
| --------------- | --- |
| I2C SDA         | D9  |
| I2C SCL         | D10 |
| GPS RX          | D8  |
| GPS TX          | D7  |
| Battery voltage | A1  |
| Buzzer control  | D0  |
| Haptic control  | D2  |
  
The three I2C devices share the same I2C bus:  
1. MS5611  
2. SH1107 128x128 OLED  
3. DuPPA i2c encoder V2.1  
GPS uses a separate hardware serial connection.  
  
⸻  
  
## I2C Encoder  
Use the DuPPA i2c encoder library:  
[ArduinoDuPPaLib](https://github.com/DuPPadotnet/ArduinoDuPPaLib)  
Do not reinvent the encoder protocol.  
Use the library for:  
* Rotary encoder rotation  
* Encoder push-button  
* Reading encoder state  
* Detecting button press  
* Detecting button hold  
The rotary encoder is the primary user interface.  
The encoder is fixed with a tri-colour LED.  This led will show a colour if both are true  

| Led          |  Battery | GPS             |
| ------------ | -------- | --------------- |
| Green        | >30%     | >=5 satellites  |
| Red          | <=30%    | Any value       |
| Yellow       | >30%     | <5 satellites   |
| flashing Red | <=10%    | Any value       |

Rules are evaluated in the order above with flashing Red taking highest priority: a battery at or below 10% must flash red even though it also satisfies the plain "<=30%" Red condition.
  
  
⸻  
  
## GPS  
Use the M10 GPS receiver connected through the hardware UART.  
Pins:  
* GPS RX = D8  
* GPS TX = D7  
The firmware should obtain at least:  
* Latitude  
* Longitude  
* Ground speed  
* Track/course  
* Altitude  
* Number of satellites  
* Fix status  
* UTC time where available  
GPS parsing must not block the main application.  
Do not use delays that prevent GPS processing.  
The GPS module should be abstracted behind a GPS class/interface so the rest of the application does not depend directly on the GPS parsing library.  
  
⸻  
  
## MS5611  
Use the MS5611 as the primary high-resolution altitude/variometer sensor.  
The MS5611 is connected through I2C.  
The firmware should:  
* Initialise the sensor  
* Read pressure  
* Read temperature  
* Calculate altitude  
* Calculate relative altitude  
* Calculate vertical speed  
* Filter the altitude signal appropriately for a variometer  
The variometer calculation must be based primarily on the MS5611 rather than GPS altitude.  
GPS altitude may be used as supporting information but should not be the primary source for instantaneous climb/sink rate.  
Avoid excessive filtering that creates unacceptable lag in the vario response.  
  
⸻  
  
## Battery Monitoring  
Battery voltage is connected to A1 through a resistor voltage divider.  
Create a configurable battery-monitor module.  
Do not hard-code the divider ratio throughout the application.  
Put calibration constants in one configuration location, for example:  

BATTERY_DIVIDER_RATIO
BATTERY_MIN_VOLTAGE
BATTERY_MAX_VOLTAGE


Convert the ADC reading into battery voltage.  
Convert battery voltage into a battery percentage.  
The battery percentage algorithm must be configurable because the actual battery chemistry and voltage range may be changed later.  
  
⸻  
  
## Buzzer  
The buzzer is controlled from D0.  
D0 drives an NPN transistor which drives the buzzer.  
Create a dedicated audio/buzzer interface.  
Do not directly manipulate the buzzer from screen code.  
The audio system should follow the sound setting defined here: [XCTracer vario sound settings](https://www.windeckfalken.de/special/xctracer/handson/main.html)
  
The implementation must be a non-blocking buzzer API.  
Avoid long delay() calls for audio.  
  
During start up the buzzer will play a startup tune, and when user selects Power Off it will play another tune.   
⸻  
  
## Display  
The display is a:  
**128x128 SH1107 OLED**  
Use the appropriate SH1107 library supported by PlatformIO.  
Create a dedicated display abstraction.  
The application must not contain raw OLED drawing commands scattered throughout unrelated classes.  
Use a screen/state architecture.  
For example:  

Screen
 ├── StartUpScreen
 ├── Pre-TakeoffScreen
 ├── VariometerScreen
 ├── WindDirectionScreen
 ├── FlightMapScreen
 ├── AltitudeTraceScreen
 ├── LandedScreen
 ├── SettingsScreen
 └── PowerOffScreen

⸻  
  
## Screen Navigation  
The rotary encoder selects the screen. While in-flight the available screens are:  
1. Variometer  
2. Wind Direction  
3. Flight Map  
4. Altitude Trace  
Rotating the encoder should move between screens.  
The selected screen should update efficiently without unnecessarily redrawing the entire display when nothing has changed.  
  
⸻  
  
## Start Up  
> **Note on mockup images:** Several sections below reference image mockups (`Attachments/*.heic`) that were pulled in from a personal notes app and were never committed to this repository. The paths do not resolve on GitHub and are not viewable by Copilot. Treat the surrounding text description as the source of truth for each screen's layout; if the visual mockups are still needed, add the actual image files under `docs/mockups/` and update the links to point there.

When the device is powered up, display a starting screen and play a short start up tune. Something like: [Startup tune reference](https://photos.app.goo.gl/19hvM5sBXp1vhVhf7)
  
After that the screen should prominently show:  
* Logo:   
![Startup screen logo mockup (not committed to repo — see note above)](Attachments/DD5038B5-119A-4907-BD9F-4833688728EE.heic)  
  
* “Variometer” plus firmware version (x.y.z)  
* GPS Satellites   
* Battery Voltage as %  
* A progressing bar based on number of satellites, where 0%=0, 100%=<min satellites>  
* “Waiting for GPS lock…”  
Less prominently, at the bottom of the screen:  
* “(c) Ian Maddocks 2026”   
Where <min satellites> is configured by the user in the settings screen (defaulting to 5).   
Once the minimum of satellites has been acquired the text should change to “Determining Altitude”. Once the MS5611 based altitude has settled and the minimum number of satellites has been reached, the screen should switch to pre-takeoff screen.   
⸻  
## Pre-takeoff  
This screen should show   
* current GPS long & latitude   
* MS5611 altitude  
* Date & time  
* A “Ready” instructions, like the screen below   
  
![Pre-takeoff screen mockup (not committed to repo)](Attachments/7C4B012A-2372-4CD1-AFAC-C5A64305F031.heic)  
  
  
From this screen the rotary encoder allows user to cycle between:  
1. Settings  
2. Power Off  
3. Pre-Takeoff  
Pressing the encoder button takes user to Variometer screen and begins flight logging.   
  
  
⸻  
  
## Power Off  
Power Off must be treated as a deliberate action.  
The user must **press and hold the rotary encoder button** to initiate power-off.  
Do not power off from a simple short button press.  
Use a configurable hold duration, for example:  

POWER_OFF_HOLD_MS


Before powering off, save any required flight/session data.  
The actual electrical power-off mechanism should be abstracted behind a PowerManager interface so the hardware implementation can be added/changed later.  
When Power Off is selected (long press), play a power off tune, persist flight data, and place ESP32 into ultra low-power mode.   
  
The power off screen should show instructions on how to power off the device.   
  
From the Power Off screen the user can user rotary encoder to cycle between:  
1. Pre-takeoff  
2. Settings  
3. Power off  
⸻  
  
## Takeoff / Landing Zone Detection  
The application must automatically detect takeoff.  
When takeoff is detected or user initiated (by pressing encoder button while in this screen):  
* Play takeoff tune  
* Play takeoff haptic sequence   
* Save the takeoff GPS latitude  
* Save the takeoff GPS longitude  
* Save the takeoff altitude  
* Save the flight start time  
* Set the takeoff location as the **LZ (Landing Zone)** reference  
* Reset flight distance/trace state  
* Start flight duration  
The LZ remains fixed for the duration of the flight.  
Do not continuously redefine the LZ.  
Takeoff detection should use a combination of information rather than a single GPS reading.  
Potential inputs include:  
* GPS ground speed  
* GPS fix quality  
* Change in altitude  
* Variometer climb rate  
* Persistence over a configurable period  
Create a dedicated FlightState / FlightDetector component so the detection algorithm can be improved later.  
Possible states:  
PREFLIGHT
TAKEOFF_DETECTED
FLIGHT
LANDING_DETECTED
POST_FLIGHT


Do not immediately trigger takeoff from one noisy GPS measurement.  
After takeoff detected or initiated by user go to ‘initial flight screen’ as defined in Change Settings, the default is Variometer).   
  
  
⸻  
  
## Landed Screen/landing Detection  
Once the GPS detects no movement for 10 seconds, the system should identify the flight has finished and display the landed screen.  
The Landed Screen is as text display of:  
* flight time  
* Distance flow  
* Avg speed  
* Highest speed  
* Highest altitude  
* Highest climb rate  
* Highest descent rate  
* Highest temp  
* Lowest temp  
To fit this data on the screen, the encoder is used to scroll, with “Back” as the first entry.   
Non-selectable entries (ie all except Back) have “-“ on the left, moving with the encoder rotation, and selectable entries (ie “Back”) has “>” on the left.   
  
Selecting Back, the default, via the encoder button press displays “Are you sure?” With a Yes & No option, where No is the default selectable option. Selecting Yes displays the pre-takeoff screen.   
  
  
⸻  
  
## Flight Data  
Create a central flight-data model containing the current flight state.  
The data model should include at least (units in parentheses):  
latitude (decimal degrees)
longitude (decimal degrees)
gpsAltitude (m)
barometricAltitude (m)
relativeAltitude (m, relative to LZ/takeoff altitude)
verticalSpeed (m/s, filtered)
groundSpeed (km/h)
track (degrees, 0-359, GPS course over ground)
satellites (count)
gpsFix (fix status/quality)
batteryVoltage (V)
batteryPercent (0-100)
flightDuration (s or ms since takeoff)
distanceFromLZ (km)
windSpeed (m/s)
windDirection (degrees, 0-359)
windConfidence (0-100%)
flightState (see FlightState enum)


Every field's unit must match the units used where that field is displayed (see Header/Footer and per-screen sections below) so no silent unit-conversion bugs creep in between the data model and the UI.  
Use appropriate types and avoid unnecessary floating-point calculations where integer/fixed-point representations are more appropriate.  
However, favour clarity over premature optimisation.  
The system should try to calculate wind speed & direction always not just when the WindDirectionScreen is active.   
  
  
⸻  
  
## Distance From LZ  
Calculate distance from the current GPS position to the saved takeoff/LZ position.  
Display distance in:  
km


Use appropriate geodesic calculations rather than simply treating latitude and longitude as Cartesian coordinates.  
The distance to LZ should also influence the automatic map scaling.  
  
⸻  
  
## Flight Map Trace  
Create a flight map screen showing an overhead trace of the flight.  
Requirements:  
* North must always be at the top.  
* The flight path must be drawn as a line.  
* The LZ/takeoff point must be marked.  
* Current aircraft position must be marked.  
* Current position should be represented by an arrow.  
* The arrow should indicate direction of travel using GPS course.  
* Map must automatically scale.  
* Distance to LZ must be displayed.  
* Map scale should adapt as the aircraft travels further from the LZ.  
Do not rotate the map with the aircraft.  
North is always UP.  
Use a local coordinate system relative to the LZ to make plotting easier.  
Convert GPS latitude/longitude into local X/Y coordinates relative to the LZ.  
For example:  
X = east/west displacement
Y = north/south displacement


The map renderer should work in these local coordinates.  
  
From the this screen the user can cycle between:  
1. Altitude Trace  
2. Variometer  
3. Wind Direction  
4. Flight Map  
⸻  
  
## Flight Altitude Trace  
Create a scrolling altitude trace.  
Requirements:  
* X axis represents time/distance through the flight.  
* Y axis represents altitude.  
* Trace moves from left to right.  
* Current position is at the right-hand side.  
* Automatically scale the altitude range.  
* Display an altitude scale as a dotted line running on the top and bottom, as per example screen below.  
* Avoid excessive redraw/flicker.  
* Maintain a history buffer.  
Use a circular/ring buffer rather than continuously allocating memory.  
The amount of history must be configurable.  
  
From the this screen the user can cycle between:  
1. Variometer  
2. Wind Direction  
3. Flight Map  
4. Altitude Trace  
  
This display should look something like:  
  
![Altitude trace screen mockup (not committed to repo)](Attachments/EE270924-BAFE-42E9-9294-B9F8298B9397.heic)  
  
⸻  
  
  
## Variometer Screen  
This screen has a prominent Altitude display, under which should be Vario and Speed.   
  
An example of how the screen should look is below, except Flugzeit should be Speed (km/h).    
![Variometer screen mockup (not committed to repo)](Attachments/A271C36D-A137-4EF9-B07D-C55C78D6226D.heic)  
  
From this screen the user can cycle between:  
1. Wind Direction  
2. Flight Map  
3. Altitude Trace  
4. Variometer  
  
⸻ 

## Wind Direction Screen  
This screen must contain 3 key graphical pieces of data: Wind Speed/direction, Direction to LZ, and Vario. Each is detailed below.   
  
From the this screen the user can cycle between:  
1. Flight Map  
2. Altitude Trace  
3. Variometer  
### Wind Direction  
This screen will have a circle, within the circle is an arrow representing the relative wind direction. See example screen below.   
  
![Wind direction screen mockup (not committed to repo)](Attachments/5336BB6B-196D-44EA-8FBC-19FDB95ACA1F.heic)  
  
If the wind direction is not know or low confidence, the arrow will not be shown.   
  
The wind estimate should be derived from available flight data.  
The wind calculation should be separated into its own component:  
WindEstimator


Do not simply display instantaneous GPS course as wind direction.  
The system should estimate wind using appropriate aircraft-motion information.  
The wind estimator should calculate:  

windSpeed
windDirection
windConfidence


Wind confidence should increase when enough reliable data has been collected.  
Confidence should decrease when the estimate becomes unreliable or insufficient.  
The wind arrow should become larger/more prominent as confidence increases.  
When confidence is low, clearly indicate that the wind estimate is uncertain.  
This spec intentionally does not fix numeric confidence bands, sample counts, or arrow-size scaling — that is an implementation decision for `WindEstimator`. Whatever thresholds are chosen, define them as named constants (not inline magic numbers) and document them in code comments.  
  
The wind speed should be displayed as a x.x value, ideally inside the arrow.   
   
### Takeoff/LZ Relative Position Indicator  
On the wind direction circle also represent the relative direction to LZ as a marker on the edge of the circle.  
  
### Vertical climb/sink indicator  
Graphically display of current vertical speed (m/s). This should be occupy the right 1/4 or 1/3 of the screen, where 0 m/s is in the middle. Above and below is a triangle outline  (flat to the RHS) getting wider to represent a larger number. The triangle above or below 0 is filled depending on the current reading.   
The filling should have a 1px gap to signify 0.5 m/s, 1.0 m/s, 1.5 m/s, 2.0 m/s and the same for negative values.   
  
The amount of fill should be proportional to the current vertical speed, positively or negatively.   
The scale should be fixed but adjustable within the code.   
This m/s data should be filtered so it is not noisy and erratic.   

⸻  

## Variometer Screen Text  
The Variometer screen should display:  
Wind: X.X m/s X%

when a valid wind estimate exists.  
When there is insufficient information, display an appropriate indication such as:  

Wind: --.- m/s 0%

Where the % value is the confidence value.   
⸻  
  
## Common In-Flight Information Header/Footer  
The following information must appear in the same screen location on all 4 primary flight screens:  
1. Flight duration  
2. Distance to LZ  
3. Speed  
4. Altitude  
5. Vertical speed  
6. Battery level  
7. Number of satellites  
Display units as:  
Flight: mins
LZ: km
Speed: km/h
Alt: m
Vario: m/s


Ideally the word Flight can be replaced with a small icon (of a clock 🕣) or “T”, and speed another icon (a car) or “S”, and Vario as a delta symbol.   
  
Battery should be represented graphically as a battery icon.  
Battery icon should be:  
* More filled at high battery level  
* Less filled at low battery level  
Satellites should be represented graphically using approximately 4 bars, similar to a mobile-phone signal indicator.  
For example:  
▂▄▆█

But slimmer bars (2px width), with 2px separation between bars.   
  
Do not rely exclusively on textual satellite count.  
The actual satellite number can additionally be shown on the Settings screen.  
  
⸻  
  
## Settings Screen  
The Settings screen should display technical information.  
As there is a lot of information, the display can scroll down with the rotary encoder rotation that move an “>” on the left of each value. To exit the setting screen to the pre-takeoff scroll to the top entry “Back” and press the encoder bottom.   
  
Scrolling past value that are not “Back” or changeable is represented by “-“ on the left of the text.   
  
Scrolling to the bottom, loops the text to the top.   
  
At minimum show:  
## GPS  
* Fix status  
* Latitude  
* Longitude  
* Altitude  
* Speed  
* Course  
* Satellites  
* Update rate if available  
* Min # satellites  
## MS5611  
* Pressure  
* Temperature  
* Barometric altitude  
* Vertical speed  
* Sensor status  
Also show useful firmware/system information such as:  
* Battery voltage  
* Battery percentage  
* Free heap memory  
* Firmware version  
   
## Changeable settings  
After the diagnostic data list the changeable data, pushing the encoder button allows the user to adjust changeable values:  
* min # of satellites  
* Background  
* Altitude Trace (mins)  
* Initial flight screen  
* Audio vario feedback
* Haptic vario feedback   
Rotating the encoder moves between the settings by showing a > to the left of the setting.   
Pressing the encoder button exists the change settings and reverts to the Settings screen.   
Long press the encoder button allows the value to be changed. Long press again save the value and reverts to cycling between changeable settings.   
When a value is changeable the value flashes at 1hz.   
* min # of satellites can be a value between 0 & 10  
* Background can select black or white  
* The Alt trace length can be adjusted from 5 to 240 mins  
* The Initial flight screen can be: Vario, Wind, Alt  
* The audio option is on or off  
* The haptic option is on or off  
  
⸻  
  
## Architecture  
Use a modular architecture similar to:  
src/
│
├── main.cpp
│
├── config/
│   └── Config.h
│
├── core/
│   ├── Application.h
│   ├── Application.cpp
│   ├── FlightData.h
│   ├── FlightState.h
│   └── SystemState.h
│
├── sensors/
│   ├── MS5611Sensor.h
│   ├── MS5611Sensor.cpp
│   ├── GPS.h
│   └── GPS.cpp
│
├── input/
│   ├── Encoder.h
│   └── Encoder.cpp
│
├── power/
│   ├── BatteryMonitor.h
│   ├── BatteryMonitor.cpp
│   ├── PowerManager.h
│   └── PowerManager.cpp
│
├── audio/
│   ├── Buzzer.h
│   └── Buzzer.cpp
│
├── flight/
│   ├── VarioCalculator.h
│   ├── VarioCalculator.cpp
│   ├── FlightDetector.h
│   ├── FlightDetector.cpp
│   ├── WindEstimator.h
│   ├── WindEstimator.cpp
│   ├── FlightRecorder.h
│   └── FlightRecorder.cpp
│
├── display/
│   ├── DisplayManager.h
│   ├── DisplayManager.cpp
│   ├── Screen.h
│   ├── VarioScreen.h
│   ├── VarioScreen.cpp
│   ├── WindDirectionScreen.h
│   ├── WindDirectionScreen.cpp
│   ├── FlightMapScreen.h
│   ├── FlightMapScreen.cpp
│   ├── AltitudeTraceScreen.h
│   ├── AltitudeTraceScreen.cpp
│   ├── LandedScreen.h
│   ├── LandedScreen.cpp
│   ├── SettingsScreen.h
│   ├── SettingsScreen.cpp
│   └── PowerOffScreen.h
│
└── utils/
    ├── GeoUtils.h
    ├── GeoUtils.cpp
    ├── RingBuffer.h
    └── Filters.h


Adapt this structure if a better architecture is clearly justified, but preserve the separation of concerns.  

> **Status:** As of this writing, `display/FlightMapScreen.*` and `utils/GeoUtils.*` / `utils/Filters.h` shown above are not yet present in `src/`. Before implementing distance-to-LZ or map-related work, check whether that logic already exists elsewhere (e.g. inline in another module) rather than assuming `GeoUtils` exists — consolidate it into `GeoUtils` if it's duplicated.
  
⸻  
  
## Main Application Loop  
The main loop should be non-blocking.  
Do NOT build the application around:  
delay(...)

Instead use:  
millis()


or equivalent timing mechanisms.  
The application should have a structure broadly similar to:  

void loop()
{
    updateGPS();
    updateMS5611();
    updateEncoder();
    updateBattery();

    updateFlightState();
    updateVario();
    updateWind();
    updateFlightTrace();

    updateAudio();
    updateScreen();
}


Individual modules should determine whether they need updating based on timing.  
Avoid unnecessary sensor reads and display redraws.  
  
⸻  
  
## Screen Interface  
Create a common screen interface.  
For example:  

class Screen
{
public:
    virtual void enter() = 0;
    virtual void update(const FlightData& data) = 0;
    virtual void draw(DisplayManager& display,
                      const FlightData& data) = 0;
    virtual void exit() = 0;
    virtual ~Screen() = default;
};


All screens should implement the same interface.  
Do not allow each screen to independently initialise hardware.  
The DisplayManager owns the display hardware.  
  
⸻  
  
## Rendering Strategy  
The display resolution is only 128x128, so use pixels efficiently.  
Design graphics specifically for a small monochrome OLED.  
Avoid overly detailed graphics.  
Prefer:  
* Simple lines  
* Circles  
* Arrows  
* Icons  
* Large readable numbers  
* Clear visual hierarchy  
The flight information should remain readable while flying.  
Do not use tiny fonts for important flight information.  
  
⸻  
  
## Data Sampling  
Use different update rates for different systems.  
For example:  
* GPS: according to receiver update rate  
* MS5611: high-rate sampling appropriate for variometer calculation  
* Vario calculation: high rate  
* Display: approximately 10–20 FPS where practical  
* Battery: relatively slow update  
* Flight trace: configurable interval  
* Wind estimation: continuously updated from valid data  
Do not force every subsystem to run at the display frame rate.  
  
⸻  
  
## Memory Management  
This is an embedded ESP32 application.  
Avoid:  
* Frequent dynamic allocation  
* String objects in high-frequency loops where avoidable  
* Large temporary buffers  
* Memory leaks  
* Unbounded containers  
Prefer:  
* Fixed-size arrays  
* Circular buffers  
* std::array where appropriate  
* Static allocation for long-lived data  
The flight trace must have a fixed maximum memory footprint.  
  
⸻  
  
## Error Handling  
The application must continue running if an individual sensor fails.  
Examples:  
If GPS fails:  
* Continue displaying barometric altitude and vario.  
* Indicate GPS unavailable.  
* Do not crash.  
If MS5611 fails:  
* Indicate sensor failure.  
* Do not use invalid altitude/vario values.  
If the encoder fails:  
* Continue displaying flight information.  
If the display fails:  
* Continue processing sensors and flight state.  
Do not allow a single peripheral failure to crash the entire application.  
  
⸻  
  
## I2C Initialisation  
Create one central I2C bus.  
Use:  

Wire.begin(I2C_SDA, I2C_SCL);


or the appropriate XIAO ESP32C3 pin definitions.  
Do not independently initialise Wire in each device class.  
All I2C devices must share the same bus instance.  
Handle I2C errors gracefully.  
  
⸻  
  
## Configuration  
Create a central configuration file.  
Example:  

namespace Config
{
    constexpr int I2C_SDA = D9;
    constexpr int I2C_SCL = D10;

    constexpr int GPS_RX = D8;
    constexpr int GPS_TX = D7;

    constexpr int BATTERY_PIN = A1;
    constexpr int BUZZER_PIN = D0;
    constexpr int HAPTIC_PIN = D2;

    constexpr float BATTERY_DIVIDER_RATIO = 2.0f;
    constexpr float BATTERY_MIN_VOLTAGE = 3.3f;
    constexpr float BATTERY_MAX_VOLTAGE = 4.2f;

    constexpr uint32_t POWER_OFF_HOLD_MS = 2000;

    constexpr float MAP_MAX_RANGE_KM = 5.0f;

    constexpr float VARIO_MAX_CLIMB = 5.0f;
    constexpr float VARIO_MAX_SINK = -5.0f;
}


Actual pin constants should use the correct PlatformIO/Arduino definitions for the XIAO ESP32C3.  
Do not duplicate hardware pin numbers throughout the project.  
  
⸻  
  
## Libraries  
Before adding a library, check whether PlatformIO already provides an appropriate maintained library.  
Known required functionality:  
* SH1107 OLED  
* DuPPA i2c encoder  
* MS5611  
* GPS/NMEA parsing  
Do not introduce unnecessary libraries.  
For the DuPPA encoder specifically, use:  
[ArduinoDuPPaLib](https://github.com/DuPPadotnet/ArduinoDuPPaLib)

⸻  
  
## Code Quality  
Use modern C++ appropriate for embedded systems.  
Prefer:  
* enum class  
* constexpr  
* RAII where appropriate  
* const correctness  
* references rather than unnecessary copies  
* clear interfaces  
* small focused classes  
* meaningful names  
Avoid:  
* global mutable state  
* magic numbers  
* deeply nested conditionals  
* duplicated display code  
* blocking delays  
* giant functions  
Comments should explain **why**, not simply repeat what the code does.  
  
⸻  
  
## Development Strategy  
Do not attempt to implement every feature in one step.  
Build the project incrementally.  
Recommended implementation order:  

## Phase 1 — Project foundation  
Create:  
* PlatformIO project  
* Configuration  
* Main application  
* I2C initialisation  
* UART/GPS initialisation  
* Basic logging  
Confirm compilation.  
## Phase 2 — Hardware drivers  
Implement and test:  
* MS5611  
* SH1107  
* DuPPA encoder  
* GPS  
* Battery ADC  
* Buzzer  
Each driver should be independently testable.  
## Phase 3 — Flight data  
Implement:  
* Barometric altitude  
* Relative altitude  
* Vertical speed  
* GPS position  
* GPS speed  
* GPS course  
* Satellite count  
* Battery level  
## Phase 4 — Flight state  
Implement:  
* Preflight  
* Takeoff detection  
* Flight  
* Landing detection  
* LZ saving  
## Phase 5 — Basic UI  
Implement the five screens and rotary encoder navigation.  
## Phase 6 — Flight map  
Implement:  
* GPS track recording  
* LZ marker  
* Current-position arrow  
* North-up map  
* Automatic scaling  
* Distance to LZ  
## Phase 7 — Altitude trace  
Implement the scrolling altitude history.  
## Phase 8 — Wind estimation  
Implement:  
* Wind speed  
* Wind direction  
* Confidence  
* Wind arrow  
## Phase 9 — Audio  
Implement:  
* Climb audio  
* Sink audio  
* Warnings  
## Phase 10 — Refinement  
Improve:  
* Filtering  
* UI readability  
* Battery calculation  
* Error handling  
* Memory use  
* Performance  
  
⸻  

## Known Regressions — Do Not Reintroduce  
The following changes were tried, caused a real regression, and were reverted (see commit `5c0a98f`, "undone most Claude", which rolled back parts of `170d5cf`). Do not reapply them without also fixing the root cause described.

### PREFLIGHT screen-cycle array must contain `StartUp`, not `PreTakeoff` — RESOLVED, see below  
`DisplayManager::handleEncoderDelta()` picks a `preflightOrder[]` array to cycle through while `currentFlightState_ == FlightState::PREFLIGHT`. It was once changed from `{StartUp, Settings, PowerOff}` to `{PreTakeoff, Settings, PowerOff}` to better match the screen names used elsewhere in this document. That change froze the UI on the StartUp screen:  
* Any encoder rotation sets `manualSelectionActive_ = true` — even while still on the StartUp screen, before GPS lock.  
* `updateScreenSelection()` only performed the automatic StartUp → PreTakeoff transition (on GPS lock) when `manualSelectionActive_` was false.  
* With `PreTakeoff` in the array instead of `StartUp`, rotating the encoder while on the StartUp screen matched nothing, so the manual cycle silently did nothing — and `manualSelectionActive_` stayed stuck `true`, so the automatic transition never fired either. The device got stuck on the StartUp screen permanently, with no manual or automatic way off it.  

**Fixed properly**: `updateScreenSelection()` now runs the StartUp → PreTakeoff transition *before* the `manualSelectionActive_` guard (it's checked unconditionally whenever `activeScreen_ == ScreenId::StartUp` and GPS is locked, with an early `return`), so a prior encoder touch can no longer block it. `preflightOrder[]` now correctly contains `PreTakeoff` again, matching the spec. If StartUp/PreTakeoff navigation ever seems stuck again, check that ordering in `updateScreenSelection()` first — the fix depends on the auto-transition check running *before*, not after, the `manualSelectionActive_` guard.

### MS5611 pressure formula: the final `>>15` applies to the whole expression, not just `OFF`  
Per the MS5611 datasheet, `P = (D1*SENS/2^21 - OFF) / 2^15` — the `>>15` must be applied after subtracting `OFF`, not only to `OFF` on its own. A change once rewrote this in `MS5611Sensor.cpp` as:  
```
(((raw * SENS) >> 21)) - (OFF >> 15)
```  
which produces incorrect pressure/altitude values. The correct form, restored by the same revert, is:  
```
(((raw * SENS) >> 21) - OFF) >> 15
```  
Keep the comment explaining this next to the code — it's exactly the kind of thing that looks like a harmless refactor.

⸻  
  
## Important Development Rule  
Before modifying an existing file, inspect the existing project.  
Do not blindly overwrite working code.  
Preserve working hardware drivers and interfaces unless there is a clear reason to change them.  
When changing an interface, update all dependent modules.  
After making significant changes:  
1. Compile the PlatformIO project.  
2. Fix all compiler warnings/errors.  
3. Check for unused code.  
4. Check for memory issues.  
5. Explain what changed.  
6. List any hardware assumptions that still need testing.  
Never claim hardware functionality has been verified if it has only been compiled.  
  
⸻  
  
## GitHub Copilot Behaviour  
When working on this project:  
1. Inspect the repository before creating files.  
2. Reuse existing code where appropriate.  
3. Do not create duplicate implementations.  
4. Keep modules loosely coupled.  
5. Prefer incremental changes.  
6. Compile after major changes.  
7. Do not silently change pin assignments.  
8. Do not silently change I2C addresses.  
9. Do not replace a working library without a reason.  
10. Do not implement placeholder functionality as if it were complete.  
When requirements are ambiguous, make the smallest reasonable assumption and clearly identify it.  
When a feature requires a hardware capability that has not yet been specified, create an abstraction/interface rather than inventing hardware behaviour.  
  
⸻  
  
## Definition of Done  
The firmware is considered complete when:  
* The ESP32C3 boots reliably.  
* All three I2C devices initialise correctly.  
* GPS communicates correctly.  
* Battery voltage is measured.  
* Buzzer operates.  
* Haptic motor operates.
* Encoder rotates through screens.  
* Encoder button works.  
* Long encoder press enters Power Off.  
* MS5611 produces stable altitude and vertical-speed data.  
* Takeoff is automatically detected.  
* LZ is automatically saved.  
* Vario screen displays climb/sink information.  
* Vario screen displays relative LZ position.  
* Wind estimate and confidence are displayed when sufficient data exists.  
* Flight map records the flight.  
* Map remains north-up.  
* Map auto-scales.  
* LZ and current aircraft position are displayed.  
* Current position shows direction of travel.  
* Distance to LZ is displayed.  
* Altitude trace works.  
* Settings screen provides GPS/MS5611 diagnostics.  
* Battery and satellite indicators work.  
* The application remains operational if an individual peripheral becomes unavailable.  
* No blocking delays interfere with GPS, sensor or UI processing.  
* The PlatformIO project builds without errors.  
* The code is modular and maintainable.  

