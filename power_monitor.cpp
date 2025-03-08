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
    if (adc_value < MIN_VALID_ADC || adc_value > MAX_VALID_ADC) {
        return false;
    }
    return true;
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

    for(int i = 0; i < samples; i++) {
        int32_t raw = analogRead(_current_pin);
        sum += raw;
        if(raw < min_val) min_val = raw;
        if(raw > max_val) max_val = raw;

        if(validateReading(raw)) {
            _valid_reading_count++;
        } else {
            _valid_reading_count = 0;
        }
        delay(1);
    }

    float quick_offset = sum / (float)samples;

    if(quick_offset >= 1500 && quick_offset <= 2500 && _valid_reading_count >= MIN_VALID_COUNT) {
        _filtered_offset = quick_offset;
        _fast_offset = quick_offset;
        _in_reconnect = true;
        _ct_connected = true;
    } else {
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
    _valid_reading_count = 0;

    for(int i = 0; i < pre_samples; i++) {
        int32_t raw = analogRead(_current_pin);
        pre_sum += raw;
        if(raw > raw_max) raw_max = raw;
        if(raw < raw_min) raw_min = raw;

        if(validateReading(raw)) {
            _valid_reading_count++;
        } else {
            _valid_reading_count = 0;
        }
    }
    float quick_check = pre_sum / pre_samples;

    float disconnect_threshold = _ct_connected ? 
                                CT_DISCONNECT_THRESHOLD : 
                                CT_DISCONNECT_THRESHOLD - CT_HYSTERESIS;

    bool possible_disconnect = (abs(quick_check - _filtered_offset) > disconnect_threshold) ||
                             (_valid_reading_count < MIN_VALID_COUNT);

    if(possible_disconnect && checkCTStateChange(false)) {
        Serial.println("CT disconnected - zero current");
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

    double offset_sum = 0;
    for(int i = 0; i < OFFSET_SAMPLES; i++) {
        int32_t raw = analogRead(_current_pin);
        offset_sum += raw;
    }
    double current_offset = offset_sum / OFFSET_SAMPLES;

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

    for(int i = 0; i < SAMPLES_PER_CYCLE; i++) {
        int32_t raw = analogRead(_current_pin);
        if(raw > raw_max) raw_max = raw;
        if(raw < raw_min) raw_min = raw;
        sum_raw += raw;

        double centered = raw - effective_offset;
        double squared = centered * centered;

        if(squared > MIN_SQUARED_ADC) {
            sum_squared += squared;
            valid_samples++;
        }
        samples_taken++;
    }

    float new_current = 0;
    double peak_to_peak = raw_max - raw_min;
    int pct_valid = (valid_samples * 100) / samples_taken;
    unsigned long now = millis();

    bool valid_signal = (valid_samples >= MIN_VALID_SAMPLES) &&
                       (peak_to_peak >= MIN_PEAK_TO_PEAK) &&
                       (pct_valid >= MIN_PCT_VALID_SAMPLES);

    if(_ct_connected && valid_signal) {
        double rms_adc = sqrt(sum_squared / samples_taken);
        double rms_voltage = rms_adc * ADC_SCALE;
        double secondary_current = rms_voltage / CURRENT_BURDEN;
        new_current = secondary_current * CT_TURNS * ICAL;

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
        }
        _current_ac = 0;
        _last_current = 0;
        _last_valid_current = 0;
    }

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
