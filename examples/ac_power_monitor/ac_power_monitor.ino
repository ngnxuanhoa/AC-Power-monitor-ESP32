#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <SD.h>
#include <SPI.h>
#include "power_monitor.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <EEPROM.h>
#include "button.h"

// Pin Definitions
#define CURRENT_PIN 36  // ADC1_CH0 (GPIO 36/VP/A0)
#define VOLTAGE_PIN 39  // ADC1_CH3 (GPIO 39/VN/A3)
#define SD_CS_PIN  5    // SD Card CS pin

// Button Pins
#define BTN_LEFT   27
#define BTN_RIGHT  26
#define BTN_BACK   25
#define BTN_SELECT 33

// Display modes
#define DISPLAY_VI    0   // Voltage and Current
#define DISPLAY_PE    1   // Power and Energy
#define DISPLAY_SET   2   // Settings

// Task handle for UI task
TaskHandle_t UITask;

// Mutex for display access
SemaphoreHandle_t displayMutex;

// EEPROM Configuration
#define EEPROM_SIZE 64        // Size of EEPROM in bytes
#define PHASE_MODE_ADDR 0     // Address to store phase mode
#define SETTINGS_VALID_ADDR 1 // Address to store settings valid flag
#define SETTINGS_VALID_VALUE 0x55 // Magic number to indicate valid settings

// Create objects
PowerMonitor powerMonitor(CURRENT_PIN, VOLTAGE_PIN, SINGLE_PHASE);
LiquidCrystal_I2C lcd(0x27, 16, 2);  // Most common address for LCD
RTC_DS3231 rtc;
String currentFileName;

// Button initialization
Button btnLeft(BTN_LEFT);
Button btnRight(BTN_RIGHT);
Button btnBack(BTN_BACK);
Button btnSelect(BTN_SELECT);

// Display state
uint8_t displayMode = DISPLAY_VI;
bool inSettings = false;
bool phaseSettingSelected = false;
volatile bool displayNeedsUpdate = false;

// UI Task running on Core 1
void UITaskCode(void * parameter) {
    Serial.println("UI Task starting on core " + String(xPortGetCoreID()));

    for(;;) {
        // Handle buttons and update display if needed
        bool buttonsChanged = handleButtons();

        if (displayNeedsUpdate || buttonsChanged) {
            if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                updateDisplay();
                displayNeedsUpdate = false;
                xSemaphoreGive(displayMutex);
            }
        }

        // Regular yield
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\nStarting AC Power Monitor...");

    // Initialize EEPROM
    if (!EEPROM.begin(EEPROM_SIZE)) {
        Serial.println("Failed to initialize EEPROM!");
        delay(1000);
    }

    // Load settings from EEPROM
    loadSettings();

    // Initialize I2C
    Wire.begin(21, 22);  // SDA = 21, SCL = 22 for ESP32
    Wire.setClock(100000);
    delay(100);

    // Initialize LCD
    lcd.init();  // Initialize display
    lcd.backlight();
    lcd.clear();
    lcd.print("Starting...");

    // Initialize RTC
    if (!rtc.begin()) {
        Serial.println("RTC failed!");
        lcd.clear();
        lcd.print("RTC Error!");
        while (1) delay(100);
    }

    // Set RTC time if needed
    if (rtc.lostPower()) {
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    // Initialize SD card
    SPI.begin();
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("SD card failed!");
        lcd.clear();
        lcd.print("SD Card Error!");
        while (1) delay(100);
    }

    // Create initial log file
    currentFileName = createFileName();
    if (!SD.exists(currentFileName)) {
        File dataFile = SD.open(currentFileName, FILE_WRITE);
        if (dataFile) {
            dataFile.println("Timestamp,Voltage(V),Current(A),Power(W),Energy(kWh)");
            dataFile.close();
        }
    }

    // Initialize power monitor
    powerMonitor.begin();

    // Create mutex for display access
    displayMutex = xSemaphoreCreateMutex();

    // Initialize buttons
    btnLeft.begin();
    btnRight.begin();
    btnBack.begin();
    btnSelect.begin();

    // Create UI task on Core 1
    xTaskCreatePinnedToCore(
        UITaskCode,
        "UITask",
        8192,
        NULL,
        1,
        &UITask,
        1  // Run on Core 1
    );

    lcd.clear();
    lcd.print("Monitor Ready");
    delay(1000);
}

void loop() {
    static unsigned long last_power_update = 0;
    static unsigned long last_log_update = 0;
    unsigned long current_time = millis();

    // Update power monitor readings every 100ms
    if (current_time - last_power_update >= 100) {
        powerMonitor.update();
        last_power_update = current_time;

        // Trigger display update
        if (xSemaphoreTake(displayMutex, 0) == pdTRUE) {
            displayNeedsUpdate = true;
            xSemaphoreGive(displayMutex);
        }
    }

    // Log data every minute
    if (current_time - last_log_update >= 60000) {
        logPowerData();
        last_log_update = current_time;
    }

    // Small delay to prevent watchdog issues
    delay(10);
}


bool handleButtons() {
    bool displayNeedsUpdate = false;

    // Update all buttons
    btnLeft.update();
    btnRight.update();
    btnBack.update();
    btnSelect.update();

    // Handle SELECT button long press
    if (!inSettings && btnSelect.isLongPress()) {
        inSettings = true;
        displayMode = DISPLAY_SET;
        displayNeedsUpdate = true;
    }

    // Handle button presses
    if (btnSelect.wasPressed()) {
        if (!inSettings) {
            // Toggle between V/I and P/E displays
            displayMode = (displayMode == DISPLAY_VI) ? DISPLAY_PE : DISPLAY_VI;
            displayNeedsUpdate = true;
        } else if (inSettings) {
            // Toggle phase setting selection
            phaseSettingSelected = !phaseSettingSelected;
            displayNeedsUpdate = true;
        }
    }

    if (inSettings && phaseSettingSelected) {
        if (btnLeft.wasPressed() || btnRight.wasPressed()) {
            // Toggle between single and three phase
            if (powerMonitor.getPhaseCount() == THREE_PHASE) {
                powerMonitor = PowerMonitor(CURRENT_PIN, VOLTAGE_PIN, SINGLE_PHASE);
            } else {
                powerMonitor = PowerMonitor(CURRENT_PIN, VOLTAGE_PIN, THREE_PHASE);
            }
            powerMonitor.begin();
            saveSettings(); // Save settings after phase mode change
            displayNeedsUpdate = true;
        }
    }

    if (inSettings && btnBack.wasPressed()) {
        inSettings = false;
        phaseSettingSelected = false;
        displayMode = DISPLAY_VI;
        displayNeedsUpdate = true;
    }

    return displayNeedsUpdate;
}

void updateDisplay() {
    if (inSettings) {
        displaySettingsScreen();
    } else {
        displayMonitorScreen();
    }
}

void displaySettingsScreen() {
    static uint8_t lastPhaseCount = 0;
    static bool lastPhaseSelected = false;

    // Only update if something changed
    if (lastPhaseCount == powerMonitor.getPhaseCount() && 
        lastPhaseSelected == phaseSettingSelected) {
        return;
    }

    // Clear only when changing screens
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Phase Mode:");
    lcd.setCursor(0, 1);
    lcd.print(powerMonitor.getPhaseCount() == THREE_PHASE ? "Three Phase" : "Single Phase");

    if (phaseSettingSelected) {
        lcd.setCursor(15, 1);
        lcd.print("*");
    }

    // Save current state
    lastPhaseCount = powerMonitor.getPhaseCount();
    lastPhaseSelected = phaseSettingSelected;
}

void displayMonitorScreen() {
    static float lastVoltage = -1;
    static float lastCurrent = -1;
    static float lastPower = -1;
    static float lastEnergy = -1;
    static bool lastMWhState = false;
    static uint8_t lastDisplayMode = 255;  // Force initial update

    float voltage = powerMonitor.getVoltageAC();
    float current = powerMonitor.getCurrentAC();
    float power = powerMonitor.getPowerW();
    float energy = powerMonitor.isAboveMWhThreshold() ? 
                   powerMonitor.getEnergyMWh() : 
                   powerMonitor.getEnergyKWh();
    bool mwhState = powerMonitor.isAboveMWhThreshold();

    // Check if values changed significantly or display mode changed
    bool needsUpdate = (abs(voltage - lastVoltage) >= 0.1) ||
                      (abs(current - lastCurrent) >= 0.01) ||
                      (abs(power - lastPower) >= 0.1) ||
                      (abs(energy - lastEnergy) >= 0.001) ||
                      (mwhState != lastMWhState) ||
                      (displayMode != lastDisplayMode);

    if (!needsUpdate) {
        return;
    }

    // Clear only when switching display modes
    if (displayMode != lastDisplayMode) {
        lcd.clear();
        lastDisplayMode = displayMode;
    }

    if (displayMode == DISPLAY_PE) {
        // Update power display with automatic unit conversion
        lcd.setCursor(0, 0);
        lcd.print("P: ");
        if (power >= 1000) {
            // Display in kW with 3 decimal places
            lcd.print(power/1000.0, 3);
            lcd.print("kW   ");
        } else {
            // Display in W with 1 decimal place
            lcd.print(power, 1);
            lcd.print("W    ");
        }

        // Update energy display
        lcd.setCursor(0, 1);
        lcd.print("E: ");
        lcd.print(energy, 3);
        lcd.print(mwhState ? "MWh  " : "kWh  ");
    } else {
        // Update voltage display
        lcd.setCursor(0, 0);
        lcd.print("V: ");
        lcd.print(voltage, 1);
        lcd.print("V");
        if (powerMonitor.getPhaseCount() == THREE_PHASE) {
            lcd.print("(3P)");
        } else {
            lcd.print("    ");  // Clear 3P if not used
        }

        // Update current display
        lcd.setCursor(0, 1);
        lcd.print("I: ");
        lcd.print(current, 2);
        lcd.print("A    ");  // Extra spaces to clear old characters
    }

    // Save current values
    lastVoltage = voltage;
    lastCurrent = current;
    lastPower = power;
    lastEnergy = energy;
    lastMWhState = mwhState;
}

String getTimeStamp() {
    DateTime now = rtc.now();
    char timestamp[20];
    sprintf(timestamp, "%02d:%02d:%02d",
            now.hour(), now.minute(), now.second());
    return String(timestamp);
}

String createFileName() {
    DateTime now = rtc.now();
    char fileName[32];
    sprintf(fileName, "/POWER_%04d%02d%02d.CSV",
            now.year(), now.month(), now.day());
    return String(fileName);
}

void logPowerData() {
    // Check if we need to create a new file for a new day
    String newFileName = createFileName();
    if (newFileName != currentFileName) {
        currentFileName = newFileName;
        File dataFile = SD.open(currentFileName, FILE_WRITE);
        if (dataFile) {
            dataFile.println("Timestamp,Voltage(V),Current(A),Power(W),Energy(kWh)");
            dataFile.close();
            Serial.println("Created new log file: " + currentFileName);
        }
    }

    // Get current readings
    float ac_voltage = powerMonitor.getVoltageAC();
    float ac_current = powerMonitor.getCurrentAC();
    float power = powerMonitor.getPowerW();
    float energy = powerMonitor.getEnergyKWh();

    String timestamp = getTimeStamp();
    String dataString = timestamp + "," +
                        String(ac_voltage, 1) + "," +
                        String(ac_current, 2) + "," +
                        String(power, 1) + "," +
                        String(energy, 3);

    File dataFile = SD.open(currentFileName, FILE_APPEND);
    if (dataFile) {
        dataFile.println(dataString);
        dataFile.close();
        Serial.println("Data logged: " + dataString);
    }
}

// Add debug messages to loadSettings() function
void loadSettings() {
    Serial.println("\nLoading settings from EEPROM...");
    if (EEPROM.read(SETTINGS_VALID_ADDR) == SETTINGS_VALID_VALUE) {
        uint8_t phaseMode = EEPROM.read(PHASE_MODE_ADDR);
        Serial.print("Read phase mode: ");
        Serial.println(phaseMode);

        if (phaseMode == THREE_PHASE || phaseMode == SINGLE_PHASE) {
            // Create new power monitor with saved phase mode
            powerMonitor = PowerMonitor(CURRENT_PIN, VOLTAGE_PIN, phaseMode);
            Serial.print("Loaded phase mode from EEPROM: ");
            Serial.println(phaseMode == THREE_PHASE ? "Three Phase" : "Single Phase");
        } else {
            Serial.println("Invalid phase mode in EEPROM, using default");
            saveSettings(); // Save default settings
        }
    } else {
        Serial.println("No valid settings found in EEPROM");
        Serial.println("Saving default settings (Single Phase)");
        saveSettings();
    }
}

// Add debug messages to saveSettings() function
void saveSettings() {
    Serial.println("\nSaving settings to EEPROM...");
    Serial.print("Phase mode: ");
    Serial.println(powerMonitor.getPhaseCount() == THREE_PHASE ? "Three Phase" : "Single Phase");

    EEPROM.write(SETTINGS_VALID_ADDR, SETTINGS_VALID_VALUE);
    EEPROM.write(PHASE_MODE_ADDR, powerMonitor.getPhaseCount());

    if (EEPROM.commit()) {
        Serial.println("Settings saved successfully");
    } else {
        Serial.println("Error saving settings!");
    }
}
