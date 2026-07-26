#pragma once

#include <Arduino.h>

#include "Config.h"

class SpeakerController;
class MidiController;
class DisplayController;

// ------------------------------------------------
// Looper
// ------------------------------------------------
//
// Nimmt das Live-Spiel auf (Note, Velocity, Instrument, Senke) und
// spielt es als Schleife ab; man spielt darüber und überlagert Schicht
// für Schicht (erst Beat, dann Melodie). Freie Länge: die erste
// Aufnahme legt die Loop-Dauer fest. Die Wiedergabe geht an dieselbe
// Senke wie beim Einspielen — Speaker (multitimbral über noteOnAs) und/
// oder MIDI. Bedienung über zwei Board-Buttons (siehe main.cpp).
class LooperController
{
public:
    void begin(SpeakerController* speaker, MidiController* midi, DisplayController* display);

    // Boot-Button: Rec -> Overdub-Zyklus (leer: Erstaufnahme starten;
    // Aufnahme: Loop fixieren + Wiedergabe; Wiedergabe: Overdub an/aus;
    // gestoppt: Wiedergabe fortsetzen)
    void toggleRecord();

    // User-Button kurz: Wiedergabe stoppen/fortsetzen (bzw. eine
    // laufende Erstaufnahme verwerfen)
    void toggleStop();

    // User-Button lang: Loop löschen
    void clear();

    // Ein Live-Note-Event mitschneiden (aus main.cpp bei pressed/
    // releasedEvent). on=false ist ein NoteOff.
    void recordEvent(bool on, uint8_t note, uint8_t velocity, uint8_t instrument, bool viaMidi,
                     uint8_t channel);

    // Aus loop() aufrufen: fällige Loop-Events abspielen
    void update();

    // Läuft gerade Aufnahme oder Wiedergabe? (u. a. um den Deep-Sleep-
    // Timer wach zu halten, solange ein Loop spielt)
    bool active() const;

    // Zustand für die Icon-Leiste: 0 = aus/gestoppt, 1 = Wiedergabe,
    // 2 = Aufnahme (Rec/Overdub)
    uint8_t displayState() const;

    // Aktuellen Loop in einen Puffer serialisieren (für den
    // Browser-Download). Gibt die Zahl geschriebener Bytes zurück
    // (0 = kein Loop bzw. Puffer zu klein). Kompaktes Binärformat mit
    // Magic + Version, damit alte Dateien erkannt werden.
    size_t serialize(uint8_t* buf, size_t max) const;

    // Einen zuvor exportierten Loop aus einem Puffer laden und
    // abspielen. false bei ungültigem Format.
    bool deserialize(const uint8_t* buf, size_t len);

private:
    enum State : uint8_t
    {
        EMPTY = 0, // kein Loop
        REC,       // Erstaufnahme läuft (Länge noch offen)
        PLAY,      // Loop spielt
        OVERDUB,   // Loop spielt UND nimmt auf
        STOPPED    // Loop vorhanden, Wiedergabe pausiert
    };

    struct LoopEvent
    {
        uint32_t timeMs;    // Offset im Loop [0, loopLen)
        uint8_t note;       // Note bzw. GM-Drumnote
        uint8_t velocity;   // 0 = NoteOff
        uint8_t instrument; // Instrument-Enum (für noteOnAs)
        uint8_t channel;    // MIDI-Kanal (bei viaMidi)
        bool viaMidi;       // Senke: MIDI (true) oder Speaker (false)
    };

    // Momentan klingende Loop-Noten — zum sicheren Abschalten am
    // Loop-Ende und bei Stop/Clear (keine hängenden Noten)
    struct ActiveNote
    {
        uint8_t note;
        uint8_t channel;
        bool viaMidi;
    };

    void fireWindow(uint32_t from, uint32_t to);
    void fireEvent(const LoopEvent& e);
    void forceOffActive();
    void setState(State s, const char* label);

    SpeakerController* _speaker = nullptr;
    MidiController* _midi       = nullptr;
    DisplayController* _display = nullptr;

    LoopEvent _events[LOOP_MAX_EVENTS] = {};
    uint16_t _count                    = 0;

    State _state        = EMPTY;
    uint32_t _recStart  = 0; // Start der Erstaufnahme
    uint32_t _loopStart = 0; // Referenz der Wiedergabeposition
    uint32_t _loopLen   = 0;
    uint32_t _lastPos   = 0; // Position im letzten update() (Wrap-Erkennung)

    ActiveNote _active[LOOP_MAX_ACTIVE] = {};
    uint8_t _activeCount                = 0;
};
