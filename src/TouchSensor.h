#pragma once

#include <Arduino.h>

class TouchSensor
{
public:
    TouchSensor(uint8_t pin, uint8_t note);

    void begin();
    void recalibrate();
    void update();
    bool pressedEvent();
    bool releasedEvent();
    bool isPressed();

    uint8_t velocity();
    uint32_t value();
    uint32_t baseline();
    uint8_t note();

private:
    uint8_t _pin;
    uint8_t _note;
    uint32_t _value;
    uint32_t _baseline;
    uint32_t _onThreshold;
    uint32_t _offThreshold;
    uint32_t _lastBaselineUpdate;

    // Glitch-Filter: Zähler aufeinanderfolgender Messungen über der
    // ON-Schwelle (siehe Config.h)
    uint8_t _aboveCount;

    // Peak-Fenster für die Anschlagsdynamik (siehe Config.h)
    bool _measuring;
    uint32_t _measureStart;
    uint32_t _peak;
    uint8_t _velocity;

    void finishMeasurement();

    bool _pressed;
    bool _pressedEvent;
    bool _releasedEvent;
};
