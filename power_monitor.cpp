#include "power_monitor.h"

PowerMonitor::PowerMonitor(uint8_t current_pin, uint8_t voltage_pin) 
    : _current_pin(current_pin), _voltage_pin(voltage_pin), _last_polarity(false) {
}

void PowerMonitor::begin() {
    Serial.println("Initializing Power Monitor...");
    pinMode(_current_pin, INPUT);
    pinMode(_voltage_pin, INPUT);
    analogReadResolution(ADC_BITS);
    analogSetAttenuation(ADC_11db);
    Serial.println("Power Monitor initialized");
}

float PowerMonitor::readVoltageSample() {
    int raw_value = analogRead(_voltage_pin);
    // No need to center value since we're reading DC
    float voltage = raw_value * VOLTAGE_CALIBRATION;

    // Single stage filtering for DC
    static float filtered_voltage = 0;
    filtered_voltage = (VOLTAGE_SLOW_ALPHA * voltage) + ((1.0f - VOLTAGE_SLOW_ALPHA) * filtered_voltage);

    // Convert back to AC RMS equivalent
    return filtered_voltage * 1.414f; // Multiply by √2 to get AC peak
}

float PowerMonitor::readCurrentSample() {
    int raw_value = analogRead(_current_pin);
    float centered_value = raw_value - (ADC_COUNTS/2);

    // Enhanced debug output for current sensor
    static unsigned long last_debug = 0;
    if (millis() - last_debug > 1000) {
        Serial.println("\n=== Current Sensor Debug ===");
        Serial.print("Raw ADC: "); Serial.println(raw_value);
        Serial.print("ADC Center: "); Serial.print(ADC_COUNTS/2);
        Serial.print(" Centered Value: "); Serial.println(centered_value);
        Serial.print("Deadband Threshold: "); Serial.println(ADC_COUNTS * 0.05); // Increased deadband
        Serial.print("Calibration Factor: "); Serial.println(CURRENT_CALIBRATION, 6);
        Serial.print("Pre-filter Current: "); Serial.println(centered_value * CURRENT_CALIBRATION, 4);
        Serial.println("==========================");
        last_debug = millis();
    }

    // Increased deadband threshold to eliminate phantom readings
    if (abs(centered_value) < (ADC_COUNTS * 0.05)) {
        return 0.0f;
    }

    // Enhanced filtering for current readings
    static float current_buffer[5] = {0};
    static int buffer_index = 0;

    // Update buffer with new reading
    current_buffer[buffer_index] = centered_value * CURRENT_CALIBRATION;
    buffer_index = (buffer_index + 1) % 5;

    // Calculate median for better noise rejection
    float temp[5];
    memcpy(temp, current_buffer, sizeof(current_buffer));
    for(int i = 0; i < 4; i++) {
        for(int j = 0; j < 4-i; j++) {
            if(temp[j] > temp[j+1]) {
                float swap = temp[j];
                temp[j] = temp[j+1];
                temp[j+1] = swap;
            }
        }
    }
    return temp[2]; // Return median value
}

void PowerMonitor::calculateParameters(int samples) {
    Serial.println("\n=== Power Calculations Debug ===");

    // Show raw sums with overflow protection
    if (_voltage_sum > 1e9 || _current_sum > 1e9 || _power_sum > 1e9) {
        Serial.println("WARNING: Sum overflow detected - resetting values");
        _voltage_sum = 0;
        _current_sum = 0;
        _power_sum = 0;
        return;
    }

    Serial.print("Raw Voltage Sum: "); Serial.println(_voltage_sum, 6);
    Serial.print("Raw Current Sum: "); Serial.println(_current_sum, 6);
    Serial.print("Raw Power Sum: "); Serial.println(_power_sum, 6);
    Serial.print("Number of Samples: "); Serial.println(samples);

    // Calculate and show RMS values with bounds checking
    float v_rms = sqrt(max(_voltage_sum / samples, 0.0f));
    float i_rms = sqrt(max(_current_sum / samples, 0.0f));

    // Validate measurements before assigning
    if (v_rms < 1000.0f && i_rms < 100.0f) {  // Reasonable limits
        _voltage_rms = v_rms;
        _current_rms = i_rms;
        _real_power = _power_sum / samples;
    } else {
        Serial.println("ERROR: Invalid RMS values calculated - keeping previous values");
        return;
    }

    Serial.print("RMS Voltage: "); Serial.println(_voltage_rms, 2);
    Serial.print("RMS Current: "); Serial.println(_current_rms, 4);
    Serial.print("Real Power (before sign check): "); Serial.println(_real_power, 2);

    // Skip calculations if current is too low
    if (_current_rms < MIN_VALID_CURRENT) {
        Serial.println("Current below minimum threshold - zeroing measurements");
        _current_rms = 0.0f;
        _real_power = 0.0f;
        _power_factor = 0.0f;
        return;
    }

    // Calculate power factor and handle direction with bounds checking
    float apparent_power = _voltage_rms * _current_rms;
    if (apparent_power > 1e6) {  // Limit to 1MW for reasonable bounds
        Serial.println("ERROR: Apparent power too high - calculation error");
        return;
    }
    Serial.print("Apparent Power: "); Serial.println(apparent_power, 2);

    if (apparent_power > 0.1) {
        _power_factor = _real_power / apparent_power;
        Serial.print("Initial Power Factor: "); Serial.println(_power_factor, 4);

        // Handle power direction
        if (_power_factor < 0) {
            Serial.println("Negative power factor detected - adjusting direction");
            _power_factor = -_power_factor;
            _real_power = abs(_real_power);
        }
    } else {
        _power_factor = 0.0f;
        Serial.println("Apparent power too low - zeroing power factor");
    }

    Serial.print("Final Power Factor: "); Serial.println(_power_factor, 4);
    Serial.print("Final Real Power: "); Serial.println(_real_power, 2);
    Serial.print("Energy Consumption: "); Serial.print(_energy_kwh, 3); Serial.println(" kWh");
    Serial.println("==============================");
}

// Zero crossing detection removed since using rectified DC voltage

void PowerMonitor::update() {
    // Update energy consumption
    unsigned long now = millis();
    if (_last_energy_update > 0 && _real_power > 0) {
        float hours = (now - _last_energy_update) / 3600000.0f; // Convert ms to hours
        _energy_kwh += (_real_power * hours) / 1000.0f; // Convert W to kW
    }
    _last_energy_update = now;

    // Reset accumulators
    _voltage_sum = 0;
    _current_sum = 0;
    _power_sum = 0;
    int samples = 0;
    unsigned long start_time = micros();

    while (samples < SAMPLES_PER_CYCLE) {
        float voltage_sample = readVoltageSample();
        float current_sample = readCurrentSample();

        // Fixed frequency since using rectified DC voltage
        _frequency = MAINS_FREQUENCY;

        // Accumulate measurements
        _voltage_sum += voltage_sample * voltage_sample;
        _current_sum += current_sample * current_sample;
        _power_sum += voltage_sample * current_sample;
        samples++;

        // Wait for next sample
        while (micros() - start_time < (samples * SAMPLING_INTERVAL));
    }

    calculateParameters(SAMPLES_PER_CYCLE);
}

bool PowerMonitor::isValid() const {
    return (_voltage_rms >= MIN_VALID_VOLTAGE && _voltage_rms <= MAX_VALID_VOLTAGE &&
            _current_rms >= MIN_VALID_CURRENT && _current_rms <= MAX_VALID_CURRENT);
}