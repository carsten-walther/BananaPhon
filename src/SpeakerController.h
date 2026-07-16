#pragma once

#include <Arduino.h>

// Polyphoner Standalone-Synth über I2S (MAX98357A): eine
// Dreieck-Stimme pro Pad, Velocity steuert die Lautstärke.
// Gleiche Schnittstelle wie der MidiController — main.cpp
// entscheidet pro Note, welche Senke sie bekommt.
class SpeakerController
{
public:
    void begin();

    void noteOn(uint8_t note, uint8_t velocity);
    void noteOff(uint8_t note);

    // Alle Stimmen ausklingen lassen — z. B. beim Wechsel in den
    // MIDI-Betrieb, damit nichts endlos weiterdudelt
    void allNotesOff();
};
