#include "sensors/BiometricSensor.h"

#include <math.h>
#include <Wire.h>

#include "config/Config.h"

namespace variometer {
namespace {

constexpr uint8_t kAddressPrimary = 0x77;
constexpr uint8_t kAddressSecondary = 0x76;

// Register addresses, per the DPS310 datasheet register map.
constexpr uint8_t kRegPsrB2 = 0x00;
constexpr uint8_t kRegTmpB2 = 0x03;
constexpr uint8_t kRegPrsCfg = 0x06;
constexpr uint8_t kRegTmpCfg = 0x07;
constexpr uint8_t kRegMeasCfg = 0x08;
constexpr uint8_t kRegCfgReg = 0x09;
constexpr uint8_t kRegReset = 0x0C;
constexpr uint8_t kRegProdId = 0x0D;
constexpr uint8_t kRegCoefBase = 0x10;
constexpr uint8_t kRegCoefSrce = 0x28;

/*
 * Reserved registers, written by applyTemperatureFix(). They are not
 * part of the datasheet's documented register map.
 */
constexpr uint8_t kRegReserved0E = 0x0E;
constexpr uint8_t kRegReserved0F = 0x0F;
constexpr uint8_t kRegReserved62 = 0x62;

constexpr int kCoefByteCount = 18;  // 0x10..0x21 inclusive

// MEAS_CFG status bits.
constexpr uint8_t kMeasCfgCoefRdy = 0x80;
constexpr uint8_t kMeasCfgSensorRdy = 0x40;
constexpr uint8_t kMeasCfgTmpRdy = 0x20;
constexpr uint8_t kMeasCfgPrsRdy = 0x10;

// MEAS_CTRL values (MEAS_CFG bits 2:0), command (single-shot) mode.
constexpr uint8_t kMeasCtrlPressure = 0x01;
constexpr uint8_t kMeasCtrlTemperature = 0x02;

// Soft reset: RESET register, bits 0x09 (SOFT_RST nibble).
constexpr uint8_t kSoftResetCommand = 0x09;

/*
 * PROD_ID (0x0D) reads 0x10 on a DPS310: product ID 0x0 in the low
 * nibble, revision 0x1 in the high nibble. Checked at startup because
 * 0x76/0x77 are shared with the BMP280/BME280/BMP388 family -- an I2C
 * ACK alone does not prove which chip answered.
 */
constexpr uint8_t kProdIdDps310 = 0x10;

// TMP_CFG's external-sensor-select bit; must mirror COEF_SRCE (0x28 bit 7)
// or the temperature -- and everything derived from it -- is wrong.
constexpr uint8_t kTmpCfgExtBit = 0x80;

/*
 * Oversampling rate 8, for both pressure and temperature.
 *
 * PRS_CFG/TMP_CFG's low nibble encodes OSR as log2(OSR): 3 -> OSR 8.
 * Chosen deliberately to stay at or below OSR 8, which needs no bit-shift
 * enable in CFG_REG -- OSR above 8 requires setting P_SHIFT/T_SHIFT there
 * as well, which is extra register-level complexity this migration
 * avoids given the coefficients above are already unverified.
 */
constexpr uint8_t kOsrSetting = 0x03;

// Scale factor for OSR 8, per the datasheet's kP/kT table -- required to
// convert the raw 24-bit two's-complement ADC value into the scaled
// value the compensation formula expects.
constexpr int32_t kScaleFactorOsr8 = 7864320;

/*
 * Conversion time budget.
 *
 * The datasheet specifies a per-measurement time that grows with OSR;
 * at OSR 8 it is on the order of ~16 ms. This value has NOT been
 * measured against real hardware, so a generous margin is used and the
 * driver additionally polls the PRS_RDY/TMP_RDY status bits rather than
 * trusting the delay alone -- if the bit is not set yet, update() simply
 * waits another pass instead of reading garbage.
 */
constexpr uint32_t kConversionTimeMs = 20;

// If the ready bit never appears within this long, give up on the
// conversion rather than stalling the state machine forever.
constexpr uint32_t kConversionTimeoutMs = 100;

constexpr float kTempMinValidC = -40.0f;
constexpr float kTempMaxValidC = 85.0f;

constexpr float kSeaLevelPressurePa = 101325.0f;
constexpr float kAltitudeExponent = 0.190294957f;

int32_t signExtend(uint32_t value, int bits) {
    const uint32_t signBit = 1u << (bits - 1);
    value &= (signBit << 1) - 1;
    return static_cast<int32_t>((value ^ signBit) - signBit);
}

}  // namespace

bool BiometricSensor::writeRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(address_);
    Wire.write(reg);
    Wire.write(value);

    const uint8_t error = Wire.endTransmission();
    if (error != 0) {
        ++counters_.convertFail;
        lastI2cError_ = error;
        return false;
    }
    return true;
}

bool BiometricSensor::readRegister(uint8_t reg, uint8_t& value) {
    return readRegisters(reg, &value, 1);
}

bool BiometricSensor::readRegisters(uint8_t reg, uint8_t* buffer, uint8_t count) {
    Wire.beginTransmission(address_);
    Wire.write(reg);

    const uint8_t error = Wire.endTransmission();
    if (error != 0) {
        ++counters_.readFail;
        lastI2cError_ = error;
        return false;
    }

    const uint8_t received = Wire.requestFrom(address_, count);
    if (received != count) {
        ++counters_.readFail;
        return false;
    }

    for (uint8_t i = 0; i < count; ++i) {
        buffer[i] = Wire.read();
    }
    return true;
}

/*
 * Calibration coefficients, packed across 0x10..0x21 per the datasheet's
 * bit layout table. See DPS310Sensor.h's class comment -- this bit
 * packing is from memory and unverified.
 */
bool BiometricSensor::readCalibrationCoefficients() {
    uint8_t raw[kCoefByteCount] = {0};

    if (!readRegisters(kRegCoefBase, raw, kCoefByteCount)) {
        DBGLN("DPS310: calibration coefficient read failed");
        return false;
    }

    bool allZero = true;
    for (uint8_t b : raw) {
        if (b != 0) {
            allZero = false;
            break;
        }
    }
    if (allZero) {
        DBGLN("DPS310: calibration coefficients all zero, treating as unprogrammed/absent device");
        return false;
    }

    // c0: 12-bit signed, spans 0x10 (8 bits) + 0x11 upper nibble.
    c0_ = signExtend((static_cast<uint32_t>(raw[0]) << 4) |
                          (static_cast<uint32_t>(raw[1]) >> 4),
                      12);

    // c1: 12-bit signed, spans 0x11 lower nibble + 0x12 (8 bits).
    c1_ = signExtend(((static_cast<uint32_t>(raw[1]) & 0x0F) << 8) |
                          static_cast<uint32_t>(raw[2]),
                      12);

    // c00: 20-bit signed, spans 0x13 (8 bits) + 0x14 (8 bits) + 0x15 upper nibble.
    c00_ = signExtend((static_cast<uint32_t>(raw[3]) << 12) |
                           (static_cast<uint32_t>(raw[4]) << 4) |
                           (static_cast<uint32_t>(raw[5]) >> 4),
                       20);

    // c10: 20-bit signed, spans 0x15 lower nibble + 0x16 (8 bits) + 0x17 (8 bits).
    c10_ = signExtend(((static_cast<uint32_t>(raw[5]) & 0x0F) << 16) |
                           (static_cast<uint32_t>(raw[6]) << 8) |
                           static_cast<uint32_t>(raw[7]),
                       20);

    c01_ = signExtend((static_cast<uint32_t>(raw[8]) << 8) | raw[9], 16);
    c11_ = signExtend((static_cast<uint32_t>(raw[10]) << 8) | raw[11], 16);
    c20_ = signExtend((static_cast<uint32_t>(raw[12]) << 8) | raw[13], 16);
    c21_ = signExtend((static_cast<uint32_t>(raw[14]) << 8) | raw[15], 16);
    c30_ = signExtend((static_cast<uint32_t>(raw[16]) << 8) | raw[17], 16);

    DBGF("DPS310: coefficients c0=%ld c1=%ld c00=%ld c10=%ld c01=%ld c11=%ld c20=%ld c21=%ld c30=%ld\n",
         static_cast<long>(c0_), static_cast<long>(c1_), static_cast<long>(c00_),
         static_cast<long>(c10_), static_cast<long>(c01_), static_cast<long>(c11_),
         static_cast<long>(c20_), static_cast<long>(c21_), static_cast<long>(c30_));

    return true;
}

/*
 * Infineon's temperature-measurement fix.
 *
 * Some DPS310 parts leave the factory with a mis-programmed fuse bit
 * that skews the temperature ADC: the compensated temperature reads
 * tens of degrees high, and because the pressure compensation is a
 * function of that same raw temperature, the altitude is biased with
 * it -- a bias that drifts as the die self-heats, which a vario sees
 * as phantom climb or sink.
 *
 * The sequence below writes undocumented reserved registers and is the
 * workaround carried by Infineon's own library and by the
 * ArduPilot/Betaflight drivers. It is documented there as harmless on
 * parts without the fault, so it is applied unconditionally rather
 * than probed for.
 *
 * Must run after the soft reset and before the calibration
 * coefficients are read.
 */
bool BiometricSensor::applyTemperatureFix() {
    return writeRegister(kRegReserved0E, 0xA5) &&
           writeRegister(kRegReserved0F, 0x96) &&
           writeRegister(kRegReserved62, 0x02) &&
           writeRegister(kRegReserved0E, 0x00) &&
           writeRegister(kRegReserved0F, 0x00);
}

/*
 * One discarded temperature conversion, run once at the end of begin().
 *
 * applyTemperatureFix() changes the sensor's internal temperature
 * configuration, and the DPS310 retains the most recent temperature
 * result internally; Infineon's driver performs the same throwaway
 * measurement straight after the fix. Blocking is acceptable here --
 * this is startup, not the sample loop.
 */
bool BiometricSensor::primeTemperature() {
    if (!writeRegister(kRegMeasCfg, kMeasCtrlTemperature)) {
        return false;
    }

    const uint32_t start = millis();
    while (millis() - start < kConversionTimeoutMs) {
        uint8_t measCfg = 0;
        if (readRegister(kRegMeasCfg, measCfg) && (measCfg & kMeasCfgTmpRdy)) {
            uint8_t raw[3] = {0, 0, 0};
            return readRegisters(kRegTmpB2, raw, 3);
        }
        delay(5);
    }

    return false;
}

void BiometricSensor::begin() {
    valid_ = false;
    state_ = State::WaitingPressure;

    pressure_ = 0.0f;
    temperature_ = 0.0f;
    altitude_ = 0.0f;
    relativeAltitude_ = 0.0f;

    pressureRaw_ = 0;
    temperatureRaw_ = 0;

    referencePressurePa_ = kSeaLevelPressurePa;
    referenceAltitude_ = 0.0f;
    referenceSet_ = false;
    calibrationValid_ = false;

    sampleSequence_ = 0;
    sampleTimeMs_ = 0;

    lastDebugMs_ = 0;

    address_ = kAddressPrimary;
    Wire.beginTransmission(address_);
    bool found = (Wire.endTransmission() == 0);

    if (!found) {
        address_ = kAddressSecondary;
        Wire.beginTransmission(address_);
        found = (Wire.endTransmission() == 0);
    }

    if (!found) {
        DBGLN("DPS310: device not found at 0x76 or 0x77");
        return;
    }

    DBGF("DPS310: using I2C address 0x%02X\n", address_);

    uint8_t prodId = 0;
    if (!readRegister(kRegProdId, prodId)) {
        DBGLN("DPS310: PROD_ID read failed");
        return;
    }

    DBGF("DPS310: PROD_ID=0x%02X\n", prodId);

    if (prodId != kProdIdDps310) {
        DBGF("DPS310: PROD_ID 0x%02X is not a DPS310 (expected 0x%02X) -- "
             "sensor marked FAILED\n",
             prodId, kProdIdDps310);
        return;
    }

    if (!writeRegister(kRegReset, kSoftResetCommand)) {
        DBGLN("DPS310: reset command failed");
        return;
    }

    // Datasheet lists time-to-first-measurement on the order of tens of
    // ms after reset; unverified, so a generous margin is used.
    delay(40);

    // Wait for the sensor to report itself and its coefficients ready
    // before touching anything else.
    bool sensorReady = false;
    bool coefReady = false;
    for (int attempt = 0; attempt < 10; ++attempt) {
        uint8_t measCfg = 0;
        if (readRegister(kRegMeasCfg, measCfg)) {
            sensorReady = (measCfg & kMeasCfgSensorRdy) != 0;
            coefReady = (measCfg & kMeasCfgCoefRdy) != 0;
            if (sensorReady && coefReady) {
                break;
            }
        }
        delay(10);
    }

    if (!sensorReady || !coefReady) {
        DBGF("DPS310: not ready after reset (sensorRdy=%d coefRdy=%d)\n",
             sensorReady ? 1 : 0, coefReady ? 1 : 0);
        return;
    }

    // Must run after the reset and before the coefficients are read.
    if (!applyTemperatureFix()) {
        DBGLN("DPS310: temperature fix sequence failed -- temperature (and the "
              "pressure compensation that uses it) may read high");
    }

    // COEF_SRCE (0x28) bit 7 records which temperature sensor produced
    // the calibration coefficients; TMP_CFG's TMP_EXT bit must match it.
    uint8_t coefSrce = 0;
    if (!readRegister(kRegCoefSrce, coefSrce)) {
        DBGLN("DPS310: COEF_SRCE read failed");
        return;
    }
    tempSensorExternal_ = (coefSrce & 0x80) != 0;

    DBGF("DPS310: COEF_SRCE=0x%02X -> coefficients from %s temperature sensor\n",
         coefSrce, tempSensorExternal_ ? "external (MEMS)" : "internal (ASIC)");

    if (!readCalibrationCoefficients()) {
        DBGLN("DPS310: calibration read failed -- sensor marked FAILED, "
              "altitude/vario unavailable.");
        return;
    }
    calibrationValid_ = true;

    if (!writeRegister(kRegPrsCfg, kOsrSetting)) {
        DBGLN("DPS310: PRS_CFG write failed");
        return;
    }

    const uint8_t tmpCfgValue =
        static_cast<uint8_t>((tempSensorExternal_ ? kTmpCfgExtBit : 0) | kOsrSetting);
    if (!writeRegister(kRegTmpCfg, tmpCfgValue)) {
        DBGLN("DPS310: TMP_CFG write failed");
        return;
    }

    // OSR 8 needs no bit-shift enable; CFG_REG stays at its reset default.
    (void)kRegCfgReg;

    // Now that TMP_CFG selects the right sensor and oversampling, burn one
    // temperature conversion so the fix above has landed on a real
    // measurement before any reading is used.
    if (!primeTemperature()) {
        DBGLN("DPS310: priming temperature measurement failed");
    }

    valid_ = true;

    writeRegister(kRegMeasCfg, kMeasCtrlPressure);
    lastTransitionMs_ = millis();

    DBGF("DPS310: initialized (PRS_CFG=0x%02X TMP_CFG=0x%02X)\n",
         kOsrSetting, tmpCfgValue);
}

void BiometricSensor::update() {
    const uint32_t now = millis();
    if (!valid_) {
        if (now - lastDebugMs_ >= Config::BIOMETRIC_SENSOR_DEBUG_INTERVAL_MS) {
            DBGLN("DPS310: sensor invalid");
            lastDebugMs_ = now;
        }
        return;
    }

    switch (state_) {
        case State::WaitingPressure: {
            if (now - lastTransitionMs_ < kConversionTimeMs) {
                return;
            }

            uint8_t measCfg = 0;
            if (!readRegister(kRegMeasCfg, measCfg)) {
                return;
            }

            if (!(measCfg & kMeasCfgPrsRdy)) {
                if (now - lastTransitionMs_ >= kConversionTimeoutMs) {
                    ++counters_.adcFailPressure;
                    writeRegister(kRegMeasCfg, kMeasCtrlPressure);
                    lastTransitionMs_ = now;
                }
                return;
            }

            uint8_t raw[3] = {0, 0, 0};
            if (!readRegisters(kRegPsrB2, raw, 3)) {
                ++counters_.adcFailPressure;
                writeRegister(kRegMeasCfg, kMeasCtrlPressure);
                lastTransitionMs_ = now;
                return;
            }

            pressureRaw_ = signExtend((static_cast<uint32_t>(raw[0]) << 16) |
                                           (static_cast<uint32_t>(raw[1]) << 8) |
                                           static_cast<uint32_t>(raw[2]),
                                       24);

            writeRegister(kRegMeasCfg, kMeasCtrlTemperature);
            lastTransitionMs_ = now;
            state_ = State::WaitingTemperature;
            break;
        }

        case State::WaitingTemperature: {
            if (now - lastTransitionMs_ < kConversionTimeMs) {
                return;
            }

            uint8_t measCfg = 0;
            if (!readRegister(kRegMeasCfg, measCfg)) {
                return;
            }

            if (!(measCfg & kMeasCfgTmpRdy)) {
                if (now - lastTransitionMs_ >= kConversionTimeoutMs) {
                    ++counters_.adcFailTemp;
                    writeRegister(kRegMeasCfg, kMeasCtrlPressure);
                    lastTransitionMs_ = now;
                    state_ = State::WaitingPressure;
                }
                return;
            }

            uint8_t raw[3] = {0, 0, 0};
            if (!readRegisters(kRegTmpB2, raw, 3)) {
                ++counters_.adcFailTemp;
                writeRegister(kRegMeasCfg, kMeasCtrlPressure);
                lastTransitionMs_ = now;
                state_ = State::WaitingPressure;
                return;
            }

            temperatureRaw_ = signExtend((static_cast<uint32_t>(raw[0]) << 16) |
                                              (static_cast<uint32_t>(raw[1]) << 8) |
                                              static_cast<uint32_t>(raw[2]),
                                          24);

            processMeasurements();

            writeRegister(kRegMeasCfg, kMeasCtrlPressure);
            lastTransitionMs_ = now;
            state_ = State::WaitingPressure;
            break;
        }
    }
}

/*
 * DPS310 compensation formula, per the datasheet:
 *
 *   Praw_sc = Praw / kP
 *   Traw_sc = Traw / kT
 *   Tcomp = c0 * 0.5 + c1 * Traw_sc
 *   Pcomp = c00 + Praw_sc * (c10 + Praw_sc * (c20 + Praw_sc * c30))
 *           + Traw_sc * c01 + Traw_sc * Praw_sc * (c11 + Praw_sc * c21)
 *
 * Both raw values were taken at OSR 8, so both use kScaleFactorOsr8.
 */
void BiometricSensor::processMeasurements() {
    const uint32_t now = millis();

    const float pRawSc = static_cast<float>(pressureRaw_) / static_cast<float>(kScaleFactorOsr8);
    const float tRawSc = static_cast<float>(temperatureRaw_) / static_cast<float>(kScaleFactorOsr8);

    const float newTemperatureC =
        static_cast<float>(c0_) * 0.5f + static_cast<float>(c1_) * tRawSc;

    if (newTemperatureC < kTempMinValidC || newTemperatureC > kTempMaxValidC) {
        ++counters_.tempReject;
        return;
    }

    const float newPressurePa =
        static_cast<float>(c00_) +
        pRawSc * (static_cast<float>(c10_) +
                  pRawSc * (static_cast<float>(c20_) + pRawSc * static_cast<float>(c30_))) +
        tRawSc * static_cast<float>(c01_) +
        tRawSc * pRawSc * (static_cast<float>(c11_) + pRawSc * static_cast<float>(c21_));

    if (!isfinite(newPressurePa) || !isfinite(newTemperatureC)) {
        DBGLN("DPS310: computed non-finite pressure/temperature, discarding sample");
        return;
    }

    if (newPressurePa < 30000.0f || newPressurePa > 120000.0f) {
        ++counters_.rangeReject;
        return;
    }

    const float newAltitude = pressureToAltitude(newPressurePa);

    if (!isfinite(newAltitude) || newAltitude < -1000.0f || newAltitude > 20000.0f) {
        ++counters_.rangeReject;
        return;
    }

    if (!referenceSet_) {
        referencePressurePa_ = newPressurePa;
        referenceAltitude_ = newAltitude;
        referenceSet_ = true;
    }

    pressure_ = newPressurePa;
    temperature_ = newTemperatureC;
    altitude_ = newAltitude;
    relativeAltitude_ = newAltitude - referenceAltitude_;

    sampleTimeMs_ = now;
    ++sampleSequence_;
    ++counters_.samples;

    if (now - lastDebugMs_ >= Config::BIOMETRIC_SENSOR_DEBUG_INTERVAL_MS) {
        DBGF("DPS310: P=%.2fPa T=%.2fC alt=%.2fm rel=%.2fm | raw P=%ld T=%ld\n",
             pressure_, temperature_, altitude_, relativeAltitude_,
             static_cast<long>(pressureRaw_), static_cast<long>(temperatureRaw_));
        lastDebugMs_ = now;
    }
}

float BiometricSensor::pressureToAltitude(float pressurePa) const {
    if (pressurePa <= 0.0f) {
        return 0.0f;
    }
    return 44330.0f * (1.0f - powf(pressurePa / kSeaLevelPressurePa, kAltitudeExponent));
}

void BiometricSensor::setTakeoffReference() {
    if (!valid_ || pressure_ <= 0.0f) {
        return;
    }

    referencePressurePa_ = pressure_;
    referenceAltitude_ = pressureToAltitude(pressure_);
    referenceSet_ = true;
    relativeAltitude_ = 0.0f;

    DBGF("DPS310: takeoff reference set to %.2f Pa\n", referencePressurePa_);
}

bool BiometricSensor::hasValidCalibration() const {
    return calibrationValid_;
}

const BiometricSensor::Counters& BiometricSensor::getCounters() const {
    return counters_;
}

void BiometricSensor::resetCounters() {
    counters_ = Counters{};
}

uint8_t BiometricSensor::getLastI2cError() const {
    return lastI2cError_;
}

int32_t BiometricSensor::getRawPressure() const {
    return pressureRaw_;
}

int32_t BiometricSensor::getRawTemperature() const {
    return temperatureRaw_;
}

uint32_t BiometricSensor::getSampleSequence() const {
    return sampleSequence_;
}

uint32_t BiometricSensor::getSampleTimeMs() const {
    return sampleTimeMs_;
}

bool BiometricSensor::isValid() const {
    return valid_;
}

float BiometricSensor::getPressure() const {
    return pressure_;
}

float BiometricSensor::getTemperature() const {
    return temperature_;
}

float BiometricSensor::getAltitude() const {
    return altitude_;
}

float BiometricSensor::getRelativeAltitude() const {
    return relativeAltitude_;
}

}  // namespace variometer
