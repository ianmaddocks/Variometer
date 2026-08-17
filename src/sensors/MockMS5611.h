#pragma once

#include <stdint.h>
#include <math.h>

class MockMS5611
{
public:

    MockMS5611()
    {
        reset();
    }

    // Returns true when a new ADC reading is available.
    // b0, b1, b2 contain the simulated MS5611 ADC bytes.
    bool readADC(uint8_t &b0, uint8_t &b1, uint8_t &b2)
    {
        const float dt = 0.05f;       // 20 Hz
        const float time = sample * dt;

        // ------------------------------------------------------------
        // Generate the "real" pressure represented by the sensor
        // ------------------------------------------------------------

        float verticalSpeed = 0.0f;

        if (time < 5.0f)
        {
            verticalSpeed = 0.0f;       // Ground
        }
        else if (time < 20.0f)
        {
            verticalSpeed = 1.5f;       // Gentle climb
        }
        else if (time < 30.0f)
        {
            verticalSpeed = 3.0f;       // Strong climb
        }
        else if (time < 40.0f)
        {
            verticalSpeed = 0.0f;       // Level
        }
        else if (time < 55.0f)
        {
            verticalSpeed = -1.5f;      // Sink
        }
        else if (time < 70.0f)
        {
            verticalSpeed = 2.0f;       // Turbulent climb
        }
        else if (time < 80.0f)
        {
            verticalSpeed = -3.0f;      // Strong sink
        }

        // Pressure change caused by altitude change.
        //
        // Approximately 11.8 Pa per metre near sea level.
        //
        // Climb -> pressure falls
        // Sink  -> pressure rises

        pressurePa += (-verticalSpeed * 11.8f) * dt;

        // ------------------------------------------------------------
        // Normal pressure noise
        // ------------------------------------------------------------

        pressurePa += randomNoise(2.0f);

        // Slow drift
        pressurePa += 0.02f * sinf(time * 0.15f);

        // ------------------------------------------------------------
        // Heavy noise bursts
        // ------------------------------------------------------------

        if (time >= 12.0f && time < 15.0f)
            pressurePa += randomNoise(15.0f);

        if (time >= 27.0f && time < 29.0f)
            pressurePa += randomNoise(30.0f);

        if (time >= 57.0f && time < 63.0f)
        {
            pressurePa += randomNoise(20.0f);
            pressurePa += 10.0f * sinf(time * 12.0f);
        }

        // ------------------------------------------------------------
        // Convert pressure to an MS5611-style 24-bit ADC value
        // ------------------------------------------------------------

        uint32_t adc = pressureToADC(pressurePa);

        // ------------------------------------------------------------
        // DELIBERATELY CORRUPT THE RAW ADC READING
        // ------------------------------------------------------------

        // Single bad readings
        if (sample == 150)
            adc += 150000;

        if (sample == 220)
            adc -= 250000;

        if (sample == 350)
            adc += 500000;

        if (sample == 500)
            adc -= 400000;

        // Several consecutive bad readings
        if (sample >= 600 && sample <= 603)
            adc += 300000;

        if (sample >= 1000 && sample <= 1005)
            adc -= 500000;

        // ------------------------------------------------------------
        // EXACTLY reproduce your 00 00 00 problem
        // ------------------------------------------------------------

        if (sample == 750 ||
            sample == 751 ||
            sample == 1250)
        {
            b0 = 0x00;
            b1 = 0x00;
            b2 = 0x00;

            sample++;
            return true;
        }

        // ------------------------------------------------------------
        // Completely unrealistic ADC values
        // ------------------------------------------------------------

        if (sample == 800)
            adc = 3000000;

        if (sample == 1200)
            adc = 16000000;

        // ------------------------------------------------------------
        // Convert 24-bit ADC value into the three MS5611 bytes
        // ------------------------------------------------------------

        b0 = (adc >> 16) & 0xFF;
        b1 = (adc >> 8)  & 0xFF;
        b2 = adc & 0xFF;

        sample++;

        return true;
    }


    void reset()
    {
        sample = 0;
        pressurePa = 101325.0f;
        randomSeed = 0x12345678;
    }


private:

    uint32_t sample;
    uint32_t randomSeed;

    float pressurePa = 101325.0f;


    // ------------------------------------------------------------
    // Deterministic pseudo-random number
    // ------------------------------------------------------------

    float randomNoise(float amplitude)
    {
        randomSeed =
            randomSeed * 1664525UL + 1013904223UL;

        float value =
            ((randomSeed >> 8) & 0xFFFF) / 32767.5f - 1.0f;

        return value * amplitude;
    }


    // ------------------------------------------------------------
    // Approximate conversion from pressure to MS5611 ADC value.
    //
    // This is ONLY for generating believable test data.
    // It does not attempt to model the complete MS5611 ADC.
    // ------------------------------------------------------------

    uint32_t pressureToADC(float pressure)
    {
        // Typical MS5611 D1 values are around 8-10 million.
        //
        // Around 1,000,000 ADC counts per 100 kPa gives a convenient
        // approximation for our test data.

        float adc = 8500000.0f +
                    (pressure - 101325.0f) * 100.0f;

        if (adc < 1.0f)
            adc = 1.0f;

        if (adc > 16777215.0f)
            adc = 16777215.0f;

        return static_cast<uint32_t>(adc);
    }
};