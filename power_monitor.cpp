#include "power_monitor.h"

PowerMonitor::PowerMonitor(uint8_t current_pin, uint8_t voltage_pin, uint8_t phase_count)
    : _current_pin(current_pin), _voltage_pin(voltage_pin),
      _phase_count(phase_count == THREE_PHASE ? THREE_PHASE : SINGLE_PHASE),
      _voltage_dc(0), _voltage_ac(0), _current_ac(0),
      _last_current(0), _filtered_offset(1880.0), _fast_offset(1880.0),
      _last_valid_current(0), _power_w(0), _energy_kwh(0),
      _last_energy_update(0), _last_valid_time(0),
      _ct_connected(true), _in_reconnect(false),
      _last_ct_state_change(0), _valid_reading_count(0) {
}

void PowerMonitor::begin() {
    pinMode(_current_pin, INPUT);
    pinMode(_voltage_pin, INPUT);
    analogReadResolution(ADC_BITS);
    analogSetAttenuation(ADC_11db);
    _last_energy_update = millis();
}

bool PowerMonitor::validateReading(int32_t adc_value) {
    return (adc_value >= MIN_VALID_ADC && adc_value <= MAX_VALID_ADC);
}

bool PowerMonitor::checkCTStateChange(bool new_state) {
    unsigned long now = millis();
    if(new_state != _ct_connected && (now - _last_ct_state_change) > CT_STATE_DEBOUNCE) {
        _last_ct_state_change = now;
        return true;
    }
    return false;
}

void PowerMonitor::resetOffsetFilters() {
    int32_t sum = 0;
    int32_t min_val = 4095, max_val = 0;
    _valid_reading_count = 0;
    const int samples = 50;

    // Take more samples for better initial estimate
    for(int i = 0; i < samples; i++) {
        int32_t raw = analogRead(_current_pin);
        sum += raw;
        if(raw < min_val) min_val = raw;
        if(raw > max_val) max_val = raw;

        if(validateReading(raw)) {
            _valid_reading_count++;
        } else {
            _valid_reading_count = 0;  // Reset counter on invalid reading
        }
        delay(1);
    }

    float quick_offset = sum / (float)samples;

    // Verify offset is reasonable and we have enough consecutive valid readings
    if(quick_offset >= 1500 && quick_offset <= 2500 && _valid_reading_count >= MIN_VALID_COUNT) {
        _filtered_offset = quick_offset;
        _fast_offset = quick_offset;
        _in_reconnect = true;
        _ct_connected = true;

        Serial.println("\nOffset filters reset:");
        Serial.print("New offset value: "); Serial.println(quick_offset, 1);
        Serial.print("ADC range during reset: ");
        Serial.print(min_val); Serial.print("-"); Serial.println(max_val);
        Serial.print("Valid readings: "); Serial.println(_valid_reading_count);
    } else {
        Serial.println("Offset reset failed - invalid readings or range");
        _ct_connected = false;
    }
}

void PowerMonitor::calculateCurrent() {
    unsigned long start_time = millis();
    double sum_squared = 0;
    int samples_taken = 0;
    int valid_samples = 0;
    int32_t raw_max = 0, raw_min = 4095;
    double sum_raw = 0;

    double pre_sum = 0;
    const int pre_samples = 20;
    _valid_reading_count = 0;  // Reset counter

    // Quick check for CT disconnection with hysteresis
    for(int i = 0; i < pre_samples; i++) {
        int32_t raw = analogRead(_current_pin);
        pre_sum += raw;
        if(raw > raw_max) raw_max = raw;
        if(raw < raw_min) raw_min = raw;

        if(validateReading(raw)) {
            _valid_reading_count++;
        } else {
            _valid_reading_count = 0;  // Reset on any invalid reading
        }
    }
    float quick_check = pre_sum / pre_samples;

    // Apply hysteresis based on current state
    float disconnect_threshold = _ct_connected ? 
                                   CT_DISCONNECT_THRESHOLD : 
                                   CT_DISCONNECT_THRESHOLD - CT_HYSTERESIS;

    bool possible_disconnect = (abs(quick_check - _filtered_offset) > disconnect_threshold) ||
                               (_valid_reading_count < MIN_VALID_COUNT);

    // Debug output
    Serial.println("\n=== CT State Check ===");
    Serial.print("Quick check ADC: "); Serial.println(quick_check, 1);
    Serial.print("Filtered offset: "); Serial.println(_filtered_offset, 1);
    Serial.print("Current threshold: "); Serial.println(disconnect_threshold);
    Serial.print("ADC difference: "); Serial.println(abs(quick_check - _filtered_offset), 1);
    Serial.print("Valid readings: "); Serial.print(_valid_reading_count);
    Serial.print("/"); Serial.println(pre_samples);
    Serial.print("ADC Range: "); Serial.print(raw_min);
    Serial.print("-"); Serial.println(raw_max);
    Serial.print("Possible disconnect: "); Serial.println(possible_disconnect ? "YES" : "NO");

    if(possible_disconnect && checkCTStateChange(false)) {
        Serial.println("CT disconnect detected");
        _ct_connected = false;
        _current_ac = 0;
        _last_current = 0;
        _last_valid_current = 0;
        _in_reconnect = false;
        return;
    } else if(!possible_disconnect && !_ct_connected && checkCTStateChange(true)) {
        Serial.println("CT reconnect detected - starting validation");
        resetOffsetFilters();
        return;
    }

    // Calculate offset
    double offset_sum = 0;
    for(int i = 0; i < OFFSET_SAMPLES; i++) {
        int32_t raw = analogRead(_current_pin);
        offset_sum += raw;
    }
    double current_offset = offset_sum / OFFSET_SAMPLES;

    // Update filters based on state
    if(_in_reconnect) {
        _fast_offset = (_fast_offset * 0.5) + (current_offset * 0.5);
        _filtered_offset = (_filtered_offset * 0.8) + (current_offset * 0.2);
    } else {
        _fast_offset = (_fast_offset * (1.0 - FAST_FILTER)) + 
                      (current_offset * FAST_FILTER);
        _filtered_offset = (_filtered_offset * (1.0 - SLOW_FILTER)) + 
                         (current_offset * SLOW_FILTER);
    }

    double effective_offset = (_fast_offset * 0.3) + (_filtered_offset * 0.7);

    // Debug output
    Serial.println("\n=== Offset Calibration ===");
    Serial.print("Raw Offset ADC: "); Serial.println(current_offset, 1);
    Serial.print("Fast Offset ADC: "); Serial.println(_fast_offset, 1);
    Serial.print("Slow Offset ADC: "); Serial.println(_filtered_offset, 1);
    Serial.print("Effective Offset: "); Serial.println(effective_offset, 1);

    // Main sampling loop
    for(int i = 0; i < SAMPLES_PER_CYCLE; i++) {
        int32_t raw = analogRead(_current_pin);
        if(raw > raw_max) raw_max = raw;
        if(raw < raw_min) raw_min = raw;
        sum_raw += raw;

        double centered = raw - effective_offset;
        double squared = centered * centered;

        if(i % 200 == 0) {
            Serial.print("Sample "); Serial.print(i);
            Serial.print(" Raw ADC: "); Serial.print(raw);
            Serial.print(" Centered: "); Serial.print(centered, 1);
            Serial.print(" Squared: "); Serial.print(squared, 1);
            Serial.println(squared > MIN_SQUARED_ADC ? " (counted)" : " (ignored)");
        }

        if(squared > MIN_SQUARED_ADC) {
            sum_squared += squared;
            valid_samples++;
        }
        samples_taken++;
    }

    // Signal validation
    float new_current = 0;
    double peak_to_peak = raw_max - raw_min;
    int pct_valid = (valid_samples * 100) / samples_taken;
    unsigned long now = millis();

    Serial.println("\n=== Signal Validation ===");
    Serial.print("Time (ms): "); Serial.println(now - start_time);
    Serial.print("Samples: "); Serial.print(samples_taken);
    Serial.print(" ("); Serial.print(valid_samples);
    Serial.print(" valid = "); Serial.print(pct_valid); Serial.println("%)");
    Serial.print("ADC Range: "); Serial.print(raw_min); Serial.print("-");
    Serial.println(raw_max);
    Serial.print("Peak-to-Peak: "); Serial.println(peak_to_peak, 1);

    bool valid_signal = (valid_samples >= MIN_VALID_SAMPLES) &&
                       (peak_to_peak >= MIN_PEAK_TO_PEAK) &&
                       (pct_valid >= MIN_PCT_VALID_SAMPLES);

    if(_ct_connected && valid_signal) {
        double rms_adc = sqrt(sum_squared / samples_taken);
        double rms_voltage = rms_adc * ADC_SCALE;
        double secondary_current = rms_voltage / CURRENT_BURDEN;
        new_current = secondary_current * CT_TURNS * ICAL;

        Serial.println("\n=== Current Calculation ===");
        Serial.print("RMS ADC: "); Serial.println(rms_adc, 1);
        Serial.print("RMS Voltage: "); Serial.print(rms_voltage * 1000.0);
        Serial.println(" mV");
        Serial.print("Secondary Current: "); Serial.print(secondary_current * 1000.0);
        Serial.println(" mA");
        Serial.print("Primary Current: "); Serial.print(new_current, 3);
        Serial.println(" A");

        // Extra smoothing during reconnect
        if(_in_reconnect) {
            _current_ac = (_last_current * 0.98) + (new_current * 0.02);
            if(++samples_taken > 50) _in_reconnect = false;
        } else {
            _current_ac = (_last_current * SMOOTHING_FACTOR) + 
                         (new_current * (1.0 - SMOOTHING_FACTOR));
        }

        _last_current = _current_ac;
        _last_valid_current = new_current;
        _last_valid_time = now;
    } else {
        if(!_ct_connected) {
            Serial.println("CT disconnected - zero current");
        } else {
            Serial.println("\nNo valid AC signal detected");
        }
        _current_ac = 0;
        _last_current = 0;
        _last_valid_current = 0;
    }

    Serial.print("Final Current: "); Serial.print(_current_ac, 3);
    Serial.println(" A");
    Serial.print("V: "); Serial.print(_voltage_ac, 1);
    Serial.print("V, I: "); Serial.print(_current_ac, 2);
    Serial.println("A");

    updateEnergy();
}

void PowerMonitor::updateEnergy() {
    unsigned long now = millis();
    float elapsed_hours = (now - _last_energy_update) / 3600000.0;

    if (_phase_count == THREE_PHASE) {
        _power_w = THREE_PHASE_FACTOR * _voltage_ac * _current_ac;
    } else {
        _power_w = _voltage_ac * _current_ac;
    }

    _energy_kwh += (_power_w * elapsed_hours * WH_TO_KWH);
    _last_energy_update = now;
}

void PowerMonitor::sampleVoltage() {
    uint32_t sum = 0;
    const int samples = 100;
    for(int i = 0; i < samples; i++) {
        sum += analogRead(_voltage_pin);
    }
    float base_voltage = (sum / samples) * ADC_SCALE * 101.70;

    if (_phase_count == THREE_PHASE) {
        _voltage_ac = base_voltage * THREE_PHASE_FACTOR;
    } else {
        _voltage_ac = base_voltage;
    }
}

void PowerMonitor::update() {
    sampleVoltage();
    calculateCurrent();
}

float PowerMonitor::calculatePowerFactor() {
    return 1.0;  // Placeholder for future implementation
}
