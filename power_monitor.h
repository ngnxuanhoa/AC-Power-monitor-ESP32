#ifndef POWER_MONITOR_H
#define POWER_MONITOR_H

#include <Arduino.h>

// Pin Definitions
#define CURRENT_PIN 36  // ADC1_CH0 (GPIO 36/VP/A0)
#define VOLTAGE_PIN 39  // ADC1_CH3 (GPIO 39/VN/A3)

// ADC Configuration
#define ADC_BITS    12
#define ADC_COUNTS  (1<<ADC_BITS)

// Sampling Configuration
#define SAMPLES_PER_CYCLE 100
#define MAINS_FREQUENCY   50  // Hz
#define SAMPLING_INTERVAL (1000000 / (MAINS_FREQUENCY * SAMPLES_PER_CYCLE)) // in microseconds

// Calibration Constants - Adjusted based on observed measurements
#define VOLTAGE_CALIBRATION 0.0596f  // Adjusted for actual 230V measurement (0.055 * 1.083)
#define CURRENT_CALIBRATION 0.0481f  // Adjusted based on actual 5.4A measurement (0.0139 * 3.46)
#define PHASE_CALIBRATION   1.7f     // Phase correction in degrees

// Filtering Constants
#define VOLTAGE_FAST_ALPHA  0.2f    // Fast filter response
#define VOLTAGE_SLOW_ALPHA  0.05f   // Slow filter for stability
#define CURRENT_ALPHA      0.1f     // Current filter response



// Error Checking - Tightened ranges for better validation
#define MIN_VALID_VOLTAGE 200.0f  // Minimum expected mains voltage
#define MAX_VALID_VOLTAGE 260.0f  // Maximum expected mains voltage
#define MIN_VALID_CURRENT 0.05f   // Minimum measurable current
#define MAX_VALID_CURRENT 100.0f  // Maximum measurable current

class PowerMonitor {
public:
    PowerMonitor(uint8_t current_pin, uint8_t voltage_pin);
    void begin();
    void update();
    float getVoltageRMS() const { return _voltage_rms; }
    float getCurrentRMS() const { return _current_rms; }
    float getRealPower() const { return _real_power; }
    float getPowerFactor() const { return _power_factor; }
    float getFrequency() const { return _frequency; }
    float getEnergyKWh() const { return _energy_kwh; }
    void resetEnergy() { _energy_kwh = 0.0f; }
    bool isValid() const;

private:
    // Pins
    uint8_t _current_pin;
    uint8_t _voltage_pin;

    // Measurements
    float _voltage_rms = 0;
    float _current_rms = 0;
    float _real_power = 0;
    float _power_factor = 0;
    float _frequency = 0;
    float _energy_kwh = 0.0f;
    unsigned long _last_energy_update = 0;

    // Internal state
    bool _last_polarity = false;
    unsigned long _crossing_start = 0;

    // Accumulator variables
    float _voltage_sum = 0;
    float _current_sum = 0;
    float _power_sum = 0;

    // Internal methods
    float readVoltageSample();
    float readCurrentSample();
    bool detectZeroCrossing(float voltage_sample, unsigned long& crossing_time);
    void calculateParameters(int samples);
};

#endif