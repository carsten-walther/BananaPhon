#pragma once

#include <Arduino.h>

#include "Config.h"

class DisplayController
{
public:
    // Schaltet die LCD-Versorgung ein, initialisiert das Panel
    // und zeichnet den Titel.
    void begin();

    // Hinweis während der Touch-Kalibrierung
    void showCalibrating();

    // Zeichnet alle Pads im Ruhezustand (nach der Kalibrierung)
    void showPads();

    // Zeichnet ein einzelnes Pad. Gedrückt: Füllstand von unten
    // proportional zur Velocity (VU-Stil, grün/gelb/rot);
    // losgelassen: Ruhezustand.
    void drawPad(uint8_t index, bool pressed, uint8_t velocity = 0);

    // Lässt gehaltene Peak-Marker nach Ablauf der Haltezeit animiert
    // nach unten fallen — regelmäßig aus loop() aufrufen (intern auf
    // VELOCITY_PEAK_FALL_INTERVAL_MS getaktet, sonst ein No-Op).
    void updatePeaks();

    // Aktualisiert die Statuszeile; zeichnet nur bei Änderung neu.
    // `portal` = WLAN-Setup-Portal aktiv, `speaker` = Standalone-
    // Betrieb über den Lautsprecher (kein MIDI-Ziel verbunden).
    void showStatus(bool ble, bool wifi, bool rtp, bool portal = false, bool speaker = false);

    // Batterieanzeige oben rechts; zeichnet nur bei Änderung neu.
    // milliVolts: Batteriespannung (bereits mit Spannungsteiler
    // verrechnet). Werte über ~4,4 V bedeuten USB-Versorgung.
    void showBattery(uint32_t milliVolts);

private:
    bool _lastBle     = false;
    bool _lastWifi    = false;
    bool _lastRtp     = false;
    bool _lastPortal  = false;
    bool _lastSpeaker = false;

    bool _statusDrawn = false;

    int _lastBatPercent = -1;
    bool _lastUsbPower  = false;

    // Peak-Hold je Pad: Markerhöhe in px ab Pad-Unterkante (0 = kein
    // Marker), Velocity für die Markerfarbe, Ende der Haltezeit, und
    // ob das Pad gerade gedrückt ist (dann kein Marker/Fallen).
    int32_t _peakPos[NUM_SENSORS]        = {0};
    uint8_t _peakVelocity[NUM_SENSORS]   = {0};
    uint32_t _peakHoldUntil[NUM_SENSORS] = {0};
    bool _padPressed[NUM_SENSORS]        = {false};

    uint32_t _lastPeakStep = 0;
};
