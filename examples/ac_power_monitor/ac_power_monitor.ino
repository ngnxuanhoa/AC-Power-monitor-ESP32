#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SD.h>
#include <SPI.h>
#include "power_monitor.h"

// LCD I2C Configuration
#define LCD_I2C_ADDR 0x27
#define LCD_COLS 16
#define LCD_ROWS 2

// SD Card pins for ESP32
#define SD_CS_PIN     5    // SD Card CS pin
#define SD_MOSI_PIN   23   // SD Card MOSI
#define SD_MISO_PIN   19   // SD Card MISO
#define SD_CLK_PIN    18   // SD Card CLK

// Initialize LCD
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);

// Initialize PowerMonitor
PowerMonitor power_monitor(CURRENT_PIN, VOLTAGE_PIN);

// File handling variables
File dataFile;
String filename;
unsigned long lastLog = 0;
const unsigned long LOG_INTERVAL = 1000; // Log every second

// Utility functions for SD card logging
String getTimestamp() {
    unsigned long timeSeconds = millis() / 1000;
    int hours = timeSeconds / 3600;
    int minutes = (timeSeconds % 3600) / 60;
    int seconds = timeSeconds % 60;

    char timestamp[9];
    sprintf(timestamp, "%02d:%02d:%02d", hours, minutes, seconds);
    return String(timestamp);
}

String createFilename() {
    char filename[32];
    unsigned long now = millis() / 1000;
    sprintf(filename, "/POWER_%lu.CSV", now);
    return String(filename);
}

void writeHeader(File& file) {
    file.println("Timestamp,Voltage(V),Current(A),Power(W),PF,Frequency(Hz)");
    Serial.println("Wrote CSV header to log file");
}

bool setupSDCard() {
    Serial.println("Initializing SD card...");

    // Configure SPI pins for SD card
    SPI.begin(SD_CLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

    // Initialize SD card
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("SD Card initialization failed!");
        return false;
    }
    Serial.println("SD Card initialized successfully");

    // Create a new log file with timestamp
    filename = createFilename();
    dataFile = SD.open(filename, FILE_WRITE);

    if (!dataFile) {
        Serial.println("Failed to create log file!");
        return false;
    }
    Serial.print("Log file created: ");
    Serial.println(filename);

    // Write CSV header
    writeHeader(dataFile);
    dataFile.close();
    return true;
}

void logData() {
    unsigned long now = millis();
    if (now - lastLog < LOG_INTERVAL) {
        return; // Not time to log yet
    }
    lastLog = now;

    dataFile = SD.open(filename, FILE_APPEND);
    if (!dataFile) {
        Serial.println("Failed to open log file for writing!");
        return;
    }

    // Format: timestamp,voltage,current,power,pf,frequency
    String dataString = getTimestamp() + "," +
                     String(power_monitor.getVoltageRMS(), 1) + "," +
                     String(power_monitor.getCurrentRMS(), 3) + "," +
                     String(power_monitor.getRealPower(), 1) + "," +
                     String(power_monitor.getPowerFactor(), 3) + "," +
                     String(power_monitor.getFrequency(), 1);

    dataFile.println(dataString);
    dataFile.close();

    Serial.print("Logged data: ");
    Serial.println(dataString);
}

void setup() {
    Serial.begin(115200);
    Serial.println("\nAC Power Monitor Starting...");

    // Initialize LCD
    Wire.begin();
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.print("AC Power Monitor");
    lcd.setCursor(0, 1);
    lcd.print("Initializing...");
    delay(1000);

    // Initialize power monitor
    power_monitor.begin();
    Serial.println("Power monitor initialized");

    // Initialize SD card logging
    lcd.clear();
    lcd.print("SD Card Init...");
    if (!setupSDCard()) {
        lcd.clear();
        lcd.print("SD Card Failed!");
        Serial.println("SD Card initialization failed!");
        while (true) delay(1000); // Halt if SD card fails
    }
    lcd.clear();
    lcd.print("SD Card Ready");
    Serial.println("SD Card initialized successfully");
    delay(1000);
}

void loop() {
    // Update measurements
    power_monitor.update();

    static unsigned long display_toggle = 0;
    static bool show_energy = false;
    
    if (millis() - display_toggle > 3000) { // Toggle display every 3 seconds
        show_energy = !show_energy;
        display_toggle = millis();
    }
    
    lcd.clear();
    if (!show_energy) {
        // First row: Voltage and Current
        lcd.setCursor(0, 0);
        lcd.print(power_monitor.getVoltageRMS(), 1);
        lcd.print("V ");
        lcd.print(power_monitor.getCurrentRMS(), 2);
        lcd.print("A");

        // Second row: Power and PF
        lcd.setCursor(0, 1);
        lcd.print(power_monitor.getRealPower(), 0);
        lcd.print("W PF:");
        lcd.print(power_monitor.getPowerFactor(), 2);
    } else {
        // Show energy consumption
        lcd.setCursor(0, 0);
        lcd.print("Energy Usage:");
        lcd.setCursor(0, 1);
        lcd.print(power_monitor.getEnergyKWh(), 3);
        lcd.print(" kWh");
    }

    // Log data to SD card
    logData();

    // Small delay to prevent overwhelming the system
    delay(100);
}
