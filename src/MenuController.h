#pragma once

#include <Arduino.h>

class SpeakerController;
class DisplayController;

// Settings-Menü am Rotary-Encoder, zwei Modi:
//   - Blättern (nach dem Öffnen): Drehen wechselt den Parameter, ein
//     Klick geht in den Wert (bzw. löst Aktionen wie Calibrate aus).
//   - Bearbeiten: Drehen ändert den Wert, ein Klick geht zurück zum
//     Blättern.
// Das Menü öffnet immer bei „Sound". Geschlossen wirkt Drehen als
// Schnellzugriff auf die Lautstärke. Änderungen werden verzögert in den
// NVS-Flash geschrieben.
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
    bool _editing      = false; // Wert-Bearbeiten statt Blättern
    uint8_t _item      = 0;
    uint32_t _deadline = 0;

    bool _dirty      = false;
    uint32_t _saveAt = 0;

    bool _calibrateRequested = false;

    // Werkseinstellungen: erst nach einer zweiten Drehung ausführen
    bool _resetArmed = false;
};
