#ifndef POWER_MONITOR_H
#define POWER_MONITOR_H

#include <Arduino.h>

// ADC Configuration for ESP32
#define ADC_BITS        12      // ESP32 ADC resolution (12-bit gives 0-4095 range)
#define ADC_COUNTS      (1<<ADC_BITS)  // 4096 total steps
#define ADC_REFERENCE   3.3     // ESP32 ADC reference voltage
#define ADC_SCALE       0.00080566     // Calibrated scale (3.3V/4096)

// Current Measurement Constants
#define CURRENT_BURDEN  10.0    // CT burden resistor value in ohms - must match hardware
#define CT_TURNS        1000    // CT turns ratio for OPCT10ATL-1000
#define ICAL            0.897   // Recalibrated for 5.2A reference load
#define SAMPLES_PER_CYCLE 1480  // Number of samples for accurate RMS calculation

// Validation Constants
#define OFFSET_SAMPLES   100    // Increased samples for better offset stability
#define FILTER_ALPHA    0.2     // Increased filter coefficient for faster response
#define MIN_CURRENT     1.0     // Minimum current threshold (1A)
#define MIN_SQUARED_ADC 900.0   // Minimum squared ADC value (about 30 ADC steps)
#define MIN_VALID_SAMPLES 400   // Require at least 400 valid samples
#define MIN_PEAK_TO_PEAK 100.0  // Minimum ADC peak-to-peak range for valid signal
#define MIN_ADC_RANGE_LOW  1800 // Expected minimum ADC value for valid signal
#define MIN_ADC_RANGE_HIGH 2020 // Slightly increased maximum ADC value
#define MIN_PCT_VALID_SAMPLES 25 // Require at least 25% valid samples

// Energy Constants
#define ENERGY_UPDATE_INTERVAL 1000 // Update energy calculation every 1 second
#define WH_TO_KWH 0.001       // Convert Watt-hours to kWh
#define KWH_TO_MWH 0.001      // Convert kWh to MWh
#define MWH_THRESHOLD 1000.0   // Threshold for converting to MWh (1000 kWh)

class PowerMonitor {
public:
    PowerMonitor(uint8_t current_pin, uint8_t voltage_pin);
    void begin();
    void update();
    float getVoltageAC() const { return _voltage_ac; }
    float getCurrentAC() const { return _current_ac; }
    float getPowerW() const { return _power_w; }
    float getEnergyKWh() const { return _energy_kwh; }
    float getEnergyMWh() const { return _energy_kwh * KWH_TO_MWH; }
    bool isAboveMWhThreshold() const { return _energy_kwh >= MWH_THRESHOLD; }

private:
    uint8_t _current_pin;       // ADC pin for current sensor
    uint8_t _voltage_pin;       // ADC pin for voltage measurement
    float _voltage_dc;          // Raw DC voltage reading
    float _voltage_ac;          // Calculated AC voltage
    float _current_ac;          // Calculated AC current in amps
    float _last_current;        // Previous current reading for smoothing
    float _filtered_offset;     // Exponentially filtered offset
    float _power_w;            // Real power in watts
    float _energy_kwh;         // Accumulated energy in kilowatt-hours
    unsigned long _last_energy_update; // Timestamp for energy updates

    void sampleVoltage();       // Basic voltage sampling
    void calculateCurrent();    // Main current calculation routine
    void updateEnergy();        // Update energy accumulation
};

#endif
