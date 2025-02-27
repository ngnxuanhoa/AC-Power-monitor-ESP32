# ESP32 AC Power Monitor

This project implements an AC power monitor using an ESP32 microcontroller, with the following features:
- Current measurement using CT sensor (OPCT10ATL-1000) on A0 pin
- Voltage measurement using voltage divider (3x100kΩ)300kΩ + 2.2kΩ on A3 pin
- Display output on LCD 1602 via I2C
- Real-time power calculation
- Energy consumption tracking
- SD card data logging with timestamps from DS3231SN RTC

## ESP32 Advantages Over ATmega
This implementation leverages ESP32's superior capabilities:
- 12-bit ADC resolution (vs ATmega's 10-bit)
- 200 samples per cycle (vs typical 50)
- Higher processing speed for real-time calculations
- More memory for detailed calibration feedback

## Hardware Requirements
- ESP32 development board
- Current transformer OPCT10ATL-1000 with 10Ω burden resistor
- Voltage divider (300kΩ + 2.2kΩ resistors)
- LCD 1602 with I2C interface
- DS3231SN RTC module
- SD card module
- Appropriate power supply

## Pin Connections
- Current Sensor: GPIO36 (VP/A0)
- Voltage Divider: GPIO39 (VN/A3)
- LCD I2C: 
  - SDA: GPIO21
  - SCL: GPIO22
- RTC (DS3231SN):
  - SDA: GPIO21 (shared with LCD)
  - SCL: GPIO22 (shared with LCD)
- SD Card:
  - CS: GPIO5
  - MOSI: GPIO23
  - MISO: GPIO19
  - CLK: GPIO18

## Detailed Wiring Instructions
1. Current Transformer (CT) Connection:
   - Connect CT output terminals directly to 10Ω burden resistor
   - Place 10Ω burden resistor between CT terminals
   - Connect one CT terminal to GPIO36 (A0)
   - Connect second CT terminal to voltage divider midpoint (1.65V bias)(3.3V-100kΩ-midpoint-100kΩ-GND)
   - Note: This configuration provides differential measurement

2. Voltage Divider Connection:
   - Connect Live wire to 300kΩ resistor
   - Connect 300kΩ to 2.2kΩ resistor
   - Connect 2.2kΩ to GND
   - Connect GPIO39 (A3) to the junction of 300kΩ and 2.2kΩ
     
3. LCD Connection:
   - VCC to 5V
   - GND to GND
   - SDA to GPIO21
   - SCL to GPIO22

4. RTC Module (DS3231SN) Connection:
   - VCC to 3.3V
   - GND to GND
   - SDA to GPIO21 (shared with LCD I2C)
   - SCL to GPIO22 (shared with LCD I2C)
   - Note: Keep I2C pull-up resistors on RTC module

5. SD Card Module Connection:
   - VCC to 3.3V
   - GND to GND
   - CS to GPIO5
   - MOSI to GPIO23
   - MISO to GPIO19
   - SCK to GPIO18

## Project Structure
```
ac_power_monitor/
├── ac_power_monitor.ino    # Main Arduino sketch
├── power_monitor.h         # Power monitoring header
├── power_monitor.cpp       # Power monitoring implementation
├── data_logger.h          # Data logging header
└── data_logger.cpp        # Data logging implementation
```

## Local Development Setup
1. Install Arduino IDE from https://www.arduino.cc/en/software
2. Install ESP32 board support:
   - Open Arduino IDE
   - Go to File > Preferences
   - Add `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json` to Additional Board Manager URLs
   - Go to Tools > Board > Boards Manager
   - Search for "esp32" and install the ESP32 package
3. Install Required Libraries:
   - Wire (built-in)
   - LiquidCrystal I2C
   - RTClib (for DS3231)
   - SD (for SD card functionality)

## Calibration Instructions
### Initial Setup
1. Download the code from this repository
2. Open in Arduino IDE
3. Connect your ESP32 via USB
4. Compile and upload the code

## Current Measurement Verification
### Hardware Requirements
- ESP32 development board
- OPCT10ATL-1000 current transformer
- 10Ω precision burden resistor (±1%)
- Reference meter for calibration

### Test Setup
1. Connect ESP32 via USB
2. Open Serial Monitor (115200 baud)
3. Connect CT sensor to GPIO36 (VP/A0)
4. Verify ADC reference voltage is 3.3V

### No-Load Testing
Monitor Serial output for 30+ seconds:
- Expected Values:
  * ADC Min/Max: 1873-1887
  * ADC Offset: ~1880
  * Raw RMS: ~0.000
  * Final Current: 0.000A

### Load Testing (5.2A Reference)
Connect known resistive load:
- Expected Values:
  * ADC Min/Max: 1810-1950
  * ADC Average: ~1881
  * Raw RMS: 45-50
  * Final Current: ~5.2A

### Current Measurement Scaling
The current measurement uses the following scaling factors:

1. ADC Reading to Voltage:
   - 12-bit ADC (0-4095 range)
   - 3.3V reference voltage
   - ADC_SCALE = 3.3V/4096

2. Voltage to Secondary Current:
   - Using 10Ω burden resistor
   - I = V/R (Ohm's law)
   - Secondary_Current = RMS_Voltage / 10Ω

3. Secondary to Primary Current:
   - 1000:1 CT ratio (OPCT10ATL-1000)
   - Primary_Current = Secondary_Current × 1000

4. Calibration Factor:
   - ICAL = 0.897 (calibrated for 5.2A reference)
   - Fine-tuning multiplier
   - Lower value means better stability

### Current Measurement Process

1. Calculate ADC offset (zero-current reference):
   - Sample 100 readings
   - Apply exponential filter (80% old + 20% new)
   - Expected offset ~1880

2. For each measurement cycle:
   - Take 1480 samples
   - Calculate RMS voltage
   - Convert to secondary current
   - Scale to primary current
   - Apply smoothing

3. Final Current Calculation:
```cpp
rms_voltage = sqrt(sum_squared / samples) * ADC_SCALE;
secondary_current = rms_voltage / CURRENT_BURDEN;
primary_current = secondary_current * CT_TURNS * ICAL;
```

### Expected Debug Values

1. No Load:
   - ADC Min/Max: 1872-1888
   - ADC Offset: ~1880
   - Raw RMS: ~0.000
   - Secondary Current: ~0.000
   - Final Current: 0.000A

2. 5.2A Load:
   - ADC Min/Max: 1806-2017
   - Raw RMS: ~0.052
   - Secondary Current: ~0.0052
   - Expected Primary: ~5.2A
   - Expected variation: ~0.1A

### Troubleshooting
1. If readings unstable:
   - Check CT connections
   - Verify burden resistor value
   - Monitor ADC reference voltage

2. If scaling incorrect:
   - Review ADC_SCALE calculation
   - Check ICAL calibration
   - Verify burden resistor matches CURRENT_BURDEN

## Contributing
Feel free to submit issues and enhancement requests!

## Data Logging Features
The system logs power measurements to the SD card in CSV format:
- File naming: POWER_YYYYMMDD.CSV (new file each day)
- Logging interval: Every measurement cycle (~500ms)
- CSV format: Timestamp,Voltage(V),Current(A),Power(W),Energy(kWh)
- Time format: HH:MM:SS

### Troubleshooting Data Logging
1. If no file is created:
   - Check SD card is properly formatted (FAT32)
   - Verify SD card connections
   - Check CS pin configuration

2. If timestamps are incorrect:
   - Verify RTC module connections
   - Check if RTC needs battery replacement
   - Resync RTC time if needed

3. If data is missing or corrupted:
   - Check SD card write speed
   - Verify power supply stability
   - Reduce logging frequency if needed
