#pragma once

#include <Arduino.h>

// Rotary-Encoder (EC11/KY-040) über die PCNT-Hardware des ESP32-S3:
// volle Quadratur-Auswertung (x4) und Glitch-Filter laufen komplett
// in der Peripherie — kein Interrupt, keine verpassten Schritte.
class EncoderController
{
public:
    void begin();

    // Taster-Entprellung — regelmäßig aus loop() aufrufen
    void update();

    // Gedrehte Rasten seit dem letzten Aufruf (positiv = im
    // Uhrzeigersinn, negativ = dagegen); Restimpulse unterhalb einer
    // Raste werden intern aufgesammelt
    int32_t readDetents();

    // Einmaliges Ereignis: Taster wurde gedrückt (entprellt)
    bool clicked();
};
