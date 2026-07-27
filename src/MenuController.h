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

    // Hat der Nutzer „Save" bzw. „Load" ausgelöst? Geben je einmal true
    // zurück (Flag wird gelöscht). Bei Load liefert index den gewählten
    // Loop. main.cpp führt die Datei-Ein/Ausgabe aus (braucht den Looper).
    bool takeSaveRequest();
    bool takeLoadRequest(uint8_t& index);

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

    // Loop speichern/laden (von main.cpp abgeholt); _loadIndex ist der im
    // Load-Menü ausgewählte Eintrag.
    bool _saveRequested = false;
    bool _loadRequested = false;
    uint8_t _loadIndex  = 0;

    // Werkseinstellungen: erst nach einer zweiten Drehung ausführen
    bool _resetArmed = false;
};
