#include "TouchSensor.h"

#include "Config.h"

void TouchSensor::configure(uint8_t pin, uint8_t note)
{
    _pin  = pin;
    _note = note;
}

// Misst die Baseline neu und setzt die Schwellwerte. Blockiert für
// (TOUCH_CALIBRATION_SAMPLES + 1) * 10 ms — dabei nicht berühren!
void TouchSensor::recalibrate()
{
    // Erste Messung verwerfen (Sensor einschwingen lassen)
    touchRead(_pin);

    delay(10);


    uint64_t sum = 0;

    for (uint8_t i = 0; i < TOUCH_CALIBRATION_SAMPLES; i++)
    {
        sum += touchRead(_pin);

        delay(10);
    }


    _baseline = sum / TOUCH_CALIBRATION_SAMPLES;

    _onThreshold  = _baseline * TOUCH_ON_RATIO;
    _offThreshold = _baseline * TOUCH_OFF_RATIO;


    // Zustand zurücksetzen: eine vor der Kalibrierung gehaltene Note
    // erzeugt danach kein verwaistes Release-Event mehr. Das NoteOff
    // dafür verschickt der Aufrufer VOR recalibrate() (siehe main.cpp).
    _pressed       = false;
    _pressedEvent  = false;
    _releasedEvent = false;

    _measuring  = false;
    _aboveCount = 0;

    _lastBaselineUpdate = millis();
}

// Schließt das Peak-Fenster ab: Velocity aus dem Spitzenwert berechnen
// und das Press-Event auslösen.
void TouchSensor::finishMeasurement()
{
    _measuring = false;

    _pressed      = true;
    _pressedEvent = true;

    if (!ENABLE_TOUCH_VELOCITY)
    {
        _velocity = DEFAULT_VELOCITY;

        return;
    }

    // Peak relativ zur Baseline linear auf VELOCITY_MIN..VELOCITY_MAX
    // abbilden: ON-Schwelle -> MIN, RATIO_MAX -> MAX (mit Begrenzung).
    float ratio = static_cast<float>(_peak) / static_cast<float>(_baseline);

    float span = TOUCH_VELOCITY_RATIO_MAX - TOUCH_ON_RATIO;

    float t = span > 0.0f ? (ratio - TOUCH_ON_RATIO) / span : 1.0f;

    if (t < 0.0f)
    {
        t = 0.0f;
    }

    if (t > 1.0f)
    {
        t = 1.0f;
    }

    _velocity = VELOCITY_MIN + static_cast<uint8_t>(t * (VELOCITY_MAX - VELOCITY_MIN) + 0.5f);
}

void TouchSensor::update()
{
    _value = touchRead(_pin);


    _pressedEvent  = false;
    _releasedEvent = false;


    if (!_pressed && !_measuring)
    {
        // Glitch-Filter: erst nach TOUCH_CONFIRM_SAMPLES Messungen in
        // Folge über der ON-Schwelle beginnt der Anschlag — ein
        // einzelner Ausreißer löst keine Geisternote aus.
        _aboveCount = _value > _onThreshold ? _aboveCount + 1 : 0;

        if (_aboveCount >= TOUCH_CONFIRM_SAMPLES)
        {
            _aboveCount = 0;

            if (TOUCH_VELOCITY_WINDOW_MS == 0)
            {
                // Kein Fenster: sofort auslösen, Velocity aus dieser Messung
                _peak = _value;

                finishMeasurement();
            }
            else
            {
                _measuring    = true;
                _measureStart = millis();
                _peak         = _value;
            }
        }
    }
    else if (_measuring)
    {
        if (_value > _peak)
        {
            _peak = _value;
        }

        // Fenster abgelaufen — oder der Finger ist schon wieder weg
        // (sehr kurzer Tipper): in beiden Fällen jetzt auslösen, das
        // Release erledigt der Block darunter im selben Durchlauf.
        if (millis() - _measureStart >= TOUCH_VELOCITY_WINDOW_MS || _value < _offThreshold)
        {
            finishMeasurement();
        }
    }

    if (_pressed && _value < _offThreshold)
    {
        _pressed       = false;
        _releasedEvent = true;
    }


    // Baseline-Nachführung: nur im losgelassenen Zustand und nur alle
    // TOUCH_BASELINE_INTERVAL_MS einen Filterschritt — so wird langsame
    // Drift (austrocknendes Gemüse, Temperatur) ausgeglichen, während
    // eine normale Berührung die Baseline praktisch nicht bewegt.
    if (!_pressed && !_measuring && TOUCH_BASELINE_INTERVAL_MS > 0 &&
        millis() - _lastBaselineUpdate >= TOUCH_BASELINE_INTERVAL_MS)
    {
        _lastBaselineUpdate = millis();

        _baseline = (_baseline * (TOUCH_BASELINE_FILTER - 1) + _value) / TOUCH_BASELINE_FILTER;

        _onThreshold  = _baseline * TOUCH_ON_RATIO;
        _offThreshold = _baseline * TOUCH_OFF_RATIO;
    }
}

bool TouchSensor::pressedEvent()
{
    return _pressedEvent;
}

bool TouchSensor::releasedEvent()
{
    return _releasedEvent;
}

bool TouchSensor::isPressed()
{
    return _pressed;
}

uint8_t TouchSensor::velocity()
{
    return _velocity;
}

uint32_t TouchSensor::value()
{
    return _value;
}

uint32_t TouchSensor::baseline()
{
    return _baseline;
}

uint8_t TouchSensor::note()
{
    return _note;
}
