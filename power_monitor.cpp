#include "power_monitor.h"

PowerMonitor::PowerMonitor(uint8_t current_pin, uint8_t voltage_pin)
    : _current_pin(current_pin), _voltage_pin(voltage_pin),
      _voltage_dc(0), _voltage_ac(0), _current_ac(0),
      _last_current(0), _filtered_offset(1880.0),
      _power_w(0), _energy_kwh(0), _last_energy_update(0) {
}

void PowerMonitor::begin() {
    pinMode(_current_pin, INPUT);
    pinMode(_voltage_pin, INPUT);
    analogReadResolution(ADC_BITS);
    analogSetAttenuation(ADC_11db);  // For input up to 3.3V
    _last_energy_update = millis();
}

void PowerMonitor::sampleVoltage() {
    uint32_t sum = 0;
    const int samples = 100;
    for(int i = 0; i < samples; i++) {
        sum += analogRead(_voltage_pin);
        delay(1);
    }
    _voltage_ac = (sum / samples) * ADC_SCALE * 101.70; // Calibrated for 215.5V AC
}

void PowerMonitor::updateEnergy() {
    unsigned long now = millis();
    float elapsed_hours = (now - _last_energy_update) / 3600000.0; // Convert ms to hours

    // Calculate power (V * I)
    _power_w = _voltage_ac * _current_ac;

    // Accumulate energy (power * time in hours)
    _energy_kwh += (_power_w * elapsed_hours * WH_TO_KWH);

    _last_energy_update = now;
}

void PowerMonitor::calculateCurrent() {
    unsigned long start_time = micros();
    double sum_squared = 0;
    int samples_taken = 0;
    int valid_samples = 0;
    int range_violations = 0;
    int32_t raw_max = 0, raw_min = 4095;
    double sum_raw = 0;

    // Calculate initial offset
    double offset_sum = 0;
    for(int i = 0; i < OFFSET_SAMPLES; i++) {
        int32_t raw = analogRead(_current_pin);
        offset_sum += raw;
        delayMicroseconds(200);
    }
    double current_offset = offset_sum / OFFSET_SAMPLES;
    _filtered_offset = (_filtered_offset * (1.0 - FILTER_ALPHA)) + 
                        (current_offset * FILTER_ALPHA);

    // Main sampling loop
    double max_centered = 0;
    for(int i = 0; i < SAMPLES_PER_CYCLE; i++) {
        // Read raw ADC value
        int32_t raw = analogRead(_current_pin);
        if(raw > raw_max) raw_max = raw;
        if(raw < raw_min) raw_min = raw;
        sum_raw += raw;

        // Check if ADC value is in valid range
        if(raw < MIN_ADC_RANGE_LOW || raw > MIN_ADC_RANGE_HIGH) {
            range_violations++;
        }

        // Center on filtered offset
        double centered = raw - _filtered_offset;
        if(abs(centered) > abs(max_centered)) {
            max_centered = centered;
        }

        // Square the centered ADC value
        double squared = centered * centered;

        // Only accumulate if above threshold
        if(squared > MIN_SQUARED_ADC) {
            sum_squared += squared;
            valid_samples++;
        }
        samples_taken++;

        delayMicroseconds(200);
    }

    // Calculate final current if signal is valid
    float new_current = 0;
    double peak_to_peak = raw_max - raw_min;
    int pct_valid = (valid_samples * 100) / samples_taken;
    bool valid_signal = (valid_samples >= MIN_VALID_SAMPLES) &&
                       (peak_to_peak >= MIN_PEAK_TO_PEAK) &&
                       (pct_valid >= MIN_PCT_VALID_SAMPLES) &&
                       (range_violations < samples_taken / 4);

    if(valid_signal) {
        // Calculate RMS from valid samples
        double rms_adc = sqrt(sum_squared / valid_samples);
        double rms_voltage = rms_adc * ADC_SCALE;
        double secondary_current = rms_voltage / CURRENT_BURDEN;
        new_current = secondary_current * CT_TURNS * ICAL;

        if(abs(new_current) < MIN_CURRENT) {
            new_current = 0;
        }
    }

    // Apply smoothing
    _current_ac = (_last_current * (1.0 - FILTER_ALPHA)) + 
                    (new_current * FILTER_ALPHA);
    _last_current = _current_ac;

    // Calculate power and display final measurements
    _power_w = _voltage_ac * _current_ac;

    Serial.print("V: "); Serial.print(_voltage_ac, 1);
    Serial.print("V, I: "); Serial.print(_current_ac, 2);
    Serial.print("A, P: "); Serial.print(_power_w, 1);
    Serial.print("W, E: "); Serial.print(_energy_kwh, 3);
    Serial.println(isAboveMWhThreshold() ? " MWh" : " kWh");
}

void PowerMonitor::update() {
    sampleVoltage();
    calculateCurrent();
    updateEnergy();
}
