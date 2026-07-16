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

    // Aktualisiert die Statuszeile; zeichnet nur bei Änderung neu
    void showStatus(bool ble, bool wifi, bool rtp);

    // Batterieanzeige oben rechts; zeichnet nur bei Änderung neu.
    // milliVolts: Batteriespannung (bereits mit Spannungsteiler
    // verrechnet). Werte über ~4,4 V bedeuten USB-Versorgung.
    void showBattery(uint32_t milliVolts);

private:
    bool _lastBle  = false;
    bool _lastWifi = false;
    bool _lastRtp  = false;

    bool _statusDrawn = false;

    int _lastBatPercent = -1;
    bool _lastUsbPower  = false;

    // Peak-Hold: letzte Velocity je Pad (0 = noch kein Anschlag)
    uint8_t _lastVelocity[NUM_SENSORS] = {0};
};
