# ESP32 AC Power Monitor

This project implements an AC power monitor using an ESP32 microcontroller, with the following features:
- Current measurement using CT sensor (OPCT10ATL-1000) on A0 pin
- Voltage measurement using voltage divider (300kΩ + 2.2kΩ) on A3 pin
- Display output on LCD 1602 via I2C
- Real-time power factor calculation
- Frequency monitoring
- SD card data logging with CSV format output

## Hardware Requirements
- ESP32 development board
- Current transformer OPCT10ATL-1000 with 10Ω burden resistor
- Voltage divider (300kΩ + 2.2kΩ resistors)
- LCD 1602 with I2C interface
- SD card module
- Appropriate power supply

## Pin Connections
- Current Sensor: GPIO36 (VP/A0)
- Voltage Divider: GPIO39 (VN/A3)
- LCD I2C: 
  - SDA: GPIO21
  - SCL: GPIO22
- SD Card:
  - CS: GPIO5
  - MOSI: GPIO23
  - MISO: GPIO19
  - CLK: GPIO18

## Detailed Wiring Instructions
1. Current Transformer (CT) Connection:
   - Connect CT output to the 10Ω burden resistor
   - Connect one end of burden resistor to ESP32 GND
   - Connect other end to GPIO36 (A0)
   - Add a 10kΩ resistor between A0 and GND for bias

2. Voltage Divider Connection:
   - Connect Live wire to 300kΩ resistor
   - Connect 300kΩ to 2.2kΩ resistor
   - Connect 2.2kΩ to GND
   - Connect GPIO39 (A3) to the junction of 300kΩ and 2.2kΩ
   - Add a 10kΩ resistor between A3 and GND for bias

3. LCD Connection:
   - VCC to 5V
   - GND to GND
   - SDA to GPIO21
   - SCL to GPIO22

4. SD Card Module Connection:
   - VCC to 3.3V
   - GND to GND
   - CS to GPIO5
   - MOSI to GPIO23
   - MISO to GPIO19
   - SCK to GPIO18

## Project Structure
```
ac_power_monitor/
├── ac_power_monitor.ino    # Main Arduino sketch with SD logging
├── config.h               # Configuration and calibration constants
├── lib/
│   ├── power_calc.h      # Power calculation library
│   ├── power_calc.cpp
│   ├── display.h         # LCD display library
│   └── display.cpp
└── power_monitor.h        # Power monitoring library
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
   - SD library (for SD card functionality)


## Compilation in Replit
1. Make sure arduino-cli core and libraries are installed
2. Click the Run button to compile using arduino-cli
3. Download the compiled binary
4. Use esptool or similar local tool to flash the ESP32

Note: While Replit can compile the code, actual flashing needs to be done locally since hardware access isn't available in the cloud environment.

## Calibration Process
1. Voltage Calibration:
   - Connect a known voltage source (or measure with multimeter)
   - Adjust VOLTAGE_CALIBRATION in config.h
   - Current value: 0.110f = ((300k + 2.2k) / 2.2k) * (3.3V / 4096) = 137.36 * 0.0008

2. Current Calibration:
   - Use a known load (e.g., incandescent bulb)
   - Measure with calibrated meter
   - Adjust CURRENT_CALIBRATION in config.h
   - Current value: 0.2728f based on CT ratio (1000:1) and 10Ω burden resistor

3. Phase Calibration:
   - Use a purely resistive load
   - Adjust PHASE_CALIBRATION until power factor reads 1.00
   - Current value: 1.7f degrees

## Data Logging
The system logs power measurements to the SD card in CSV format:
- File naming: POWER_[timestamp].CSV
- Logging interval: 1 second
- CSV format: Timestamp,Voltage(V),Current(A),Power(W),PF,Frequency(Hz)
- Time format: HH:MM:SS

To access the logged data:
1. Power off the device
2. Remove the SD card
3. Insert into a computer
4. Open the CSV files with any spreadsheet software

## Safety Warning
⚠️ This project involves measuring AC mains voltage. Always:
- Use appropriate isolation and safety measures
- Ensure proper insulation of all connections
- Never work on live circuits
- Consider using an isolation transformer during development
- Consult a qualified electrician if unsure

## Troubleshooting
1. Invalid Readings:
   - Check burden resistor connections
   - Verify voltage divider values
   - Ensure proper grounding
   - Check calibration constants

2. No Display:
   - Verify I2C address (default 0x27)
   - Check I2C connections
   - Try I2C scanner sketch

3. Inaccurate Power Factor:
   - Adjust PHASE_CALIBRATION
   - Check CT orientation
   - Verify voltage phase sequence

## Troubleshooting SD Card Issues
1. No Data Logging:
   - Check SD card is properly inserted
   - Verify SD card is formatted as FAT32
   - Ensure proper voltage (3.3V) to SD module
   - Check SPI connections

2. Corrupted Files:
   - Use a newly formatted SD card
   - Ensure proper power supply
   - Check if card is properly initialized at startup


## Contributing
Feel free to submit issues and enhancement requests!