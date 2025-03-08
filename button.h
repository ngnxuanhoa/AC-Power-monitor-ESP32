#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>

// Button timing constants
#define DEBOUNCE_DELAY    50   // Debounce time in milliseconds
#define LONG_PRESS_TIME   1000 // Long press threshold in milliseconds

class Button {
public:
    Button(uint8_t pin);
    void begin();
    bool update();              // Returns true if state changed
    bool isPressed() const;     // Current button state
    bool wasPressed();          // True if button was just pressed
    bool wasReleased();        // True if button was just released
    bool isLongPress();        // Check for long press
    unsigned long getPressTime() const;  // How long button has been pressed

private:
    uint8_t _pin;
    bool _lastState;
    bool _currentState;
    bool _wasPressed;
    bool _wasReleased;
    unsigned long _lastDebounceTime;
    unsigned long _pressStartTime;
    bool _longPressDetected;
};

#endif
