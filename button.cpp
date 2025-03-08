#include "button.h"

Button::Button(uint8_t pin)
    : _pin(pin), _lastState(LOW), _currentState(LOW),
      _wasPressed(false), _wasReleased(false),
      _lastDebounceTime(0), _pressStartTime(0),
      _longPressDetected(false) {
}

void Button::begin() {
    pinMode(_pin, INPUT);
}

bool Button::update() {
    bool changed = false;
    bool reading = digitalRead(_pin);

    // Reset one-time flags
    _wasPressed = false;
    _wasReleased = false;

    if (reading != _lastState) {
        _lastDebounceTime = millis();
    }

    unsigned long currentTime = millis();
    if ((currentTime - _lastDebounceTime) > DEBOUNCE_DELAY) {
        if (reading != _currentState) {
            _currentState = reading;
            changed = true;

            if (_currentState == HIGH) {
                _pressStartTime = currentTime;
                _wasPressed = true;
                _longPressDetected = false;
            } else {
                _wasReleased = true;
            }
        }
    }

    // Update long press detection
    if (_currentState == HIGH && !_longPressDetected) {
        if ((currentTime - _pressStartTime) > LONG_PRESS_TIME) {
            _longPressDetected = true;
        }
    }

    _lastState = reading;
    return changed;
}

bool Button::isPressed() const {
    return _currentState == HIGH;
}

bool Button::wasPressed() {
    bool result = _wasPressed;
    _wasPressed = false;  // Clear the flag
    return result;
}

bool Button::wasReleased() {
    bool result = _wasReleased;
    _wasReleased = false;  // Clear the flag
    return result;
}

bool Button::isLongPress() {
    return _longPressDetected;
}

unsigned long Button::getPressTime() const {
    if (_currentState == HIGH) {
        return millis() - _pressStartTime;
    }
    return 0;
}
