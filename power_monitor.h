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
#define ICAL            0.963   // Calibrated for 77A test load
#define SAMPLES_PER_CYCLE 1480  // Number of samples for accurate RMS calculation

// Validation Constants
#define OFFSET_SAMPLES   100    // Increased samples for better stability
#define FAST_FILTER     0.50    // Very fast response during recalibration
#define SLOW_FILTER     0.02    // Very slow filter for stability
#define MIN_SQUARED_ADC 100.0   // Reduced for better sensitivity
#define MIN_VALID_SAMPLES 200   // Balance between stability and responsiveness
#define MIN_PEAK_TO_PEAK 40.0   // Reduced for motor loads
#define MIN_PCT_VALID_SAMPLES 15 // More permissive for inductive loads
#define SMOOTHING_FACTOR 0.95   // Strong smoothing for stability

// Reconnection Detection
#define CT_DISCONNECT_THRESHOLD 4000  // Increased threshold for better stability
#define CT_STATE_DEBOUNCE 5000      // 5 second debounce for CT state changes
#define CT_HYSTERESIS 2000         // Larger hysteresis band to prevent oscillation
#define MIN_VALID_ADC 400         // More permissive minimum ADC value
#define MAX_VALID_ADC 3600        // More permissive maximum ADC value
#define MIN_VALID_COUNT 5         // Reduced minimum valid readings for better stability

// Energy Constants
#define ENERGY_UPDATE_INTERVAL 1000 // Update energy calculation every 1 second
#define WH_TO_KWH 0.001      // Convert Watt-hours to kWh
#define KWH_TO_MWH 0.001     // Convert kWh to MWh
#define MWH_THRESHOLD 1000.0  // Threshold for converting to MWh

// Phase Configuration
#define SINGLE_PHASE 1
#define THREE_PHASE  3
#define THREE_PHASE_FACTOR 1.732 // √3 for three-phase power calculations

class PowerMonitor {
public:
    PowerMonitor(uint8_t current_pin, uint8_t voltage_pin, uint8_t phase_count = SINGLE_PHASE);
    void begin();
    void update();
    float getVoltageAC() const { return _voltage_ac; }
    float getCurrentAC() const { return _current_ac; }
    float getPowerW() const { return _power_w; }
    float getEnergyKWh() const { return _energy_kwh; }
    float getEnergyMWh() const { return _energy_kwh * KWH_TO_MWH; }
    bool isAboveMWhThreshold() const { return _energy_kwh >= MWH_THRESHOLD; }
    uint8_t getPhaseCount() const { return _phase_count; }

private:
    uint8_t _current_pin;       // ADC pin for current sensor
    uint8_t _voltage_pin;       // ADC pin for voltage measurement
    uint8_t _phase_count;       // Number of phases (1 or 3)
    float _voltage_dc;          // Raw DC voltage reading
    float _voltage_ac;          // Calculated AC voltage
    float _current_ac;          // Calculated AC current in amps
    float _last_current;        // Previous current reading for smoothing
    float _filtered_offset;     // Slow filtered offset
    float _fast_offset;         // Fast filtered offset
    float _last_valid_current;  // Last known valid current
    float _power_w;            // Real power in watts
    float _energy_kwh;         // Accumulated energy in kilowatt-hours
    unsigned long _last_energy_update; // Timestamp for energy updates
    unsigned long _last_valid_time;   // Timestamp of last valid reading
    bool _ct_connected;        // CT connection state tracking
    bool _in_reconnect;        // Flag for reconnection state
    unsigned long _last_ct_state_change; // Timestamp of last CT state change
    uint8_t _valid_reading_count;      // Counter for consecutive valid readings

    void sampleVoltage();       // Basic voltage sampling
    void calculateCurrent();    // Main current calculation routine
    void updateEnergy();        // Update energy accumulation
    float calculatePowerFactor(); // Calculate power factor (for future use)
    void resetOffsetFilters();  // Reset offset tracking after reconnect
    bool checkCTStateChange(bool new_state); // Debounce CT state changes
    bool validateReading(int32_t adc_value); // Validate single ADC reading
};

#endif
