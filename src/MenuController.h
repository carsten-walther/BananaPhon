#pragma once

#include <Arduino.h>

class SpeakerController;
class DisplayController;

// Settings-Menü am Rotary-Encoder: Klick öffnet/wechselt den
// Parameter (Lautstärke, Wellenform, Oktave), Drehen ändert den Wert.
// Geschlossen wirkt Drehen als Schnellzugriff auf die Lautstärke.
// Änderungen werden verzögert in den NVS-Flash geschrieben.
class MenuController
{
public:
    void begin(SpeakerController* speaker, DisplayController* display);

    void handleClick();
    void handleRotation(int32_t detents);

    // Timeout und verzögertes Speichern — aus loop() aufrufen
    void update();

    // Hat der Nutzer im Menü eine Rekalibrierung ausgelöst? Gibt true
    // genau einmal zurück (Flag wird dabei gelöscht) — main.cpp führt
    // die eigentliche Kalibrierung aus (sie braucht Sensoren und MIDI).
    bool takeCalibrateRequest();

private:
    void show();
    void applyVolume(int32_t detents);
    void markDirty();

    SpeakerController* _speaker = nullptr;
    DisplayController* _display = nullptr;

    bool _open         = false;
    uint8_t _item      = 0;
    uint32_t _deadline = 0;

    bool _dirty      = false;
    uint32_t _saveAt = 0;

    bool _calibrateRequested = false;
};
