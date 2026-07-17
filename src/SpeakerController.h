#pragma once

#include <Arduino.h>

// Wellenformen des Standalone-Synths
enum Waveform : uint8_t
{
    WAVE_TRIANGLE = 0,
    WAVE_SQUARE,
    WAVE_SAW,
    WAVE_SINE,
    WAVE_CHIP, // 8-Bit/Chiptune: 25%-Puls mit 8-Bit-Quantisierung

    WAVE_COUNT
};

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

    // Gesamtlautstärke zur Laufzeit (0.0–1.0), z. B. vom Encoder;
    // Startwert ist SPEAKER_MASTER_VOLUME aus der Config
    void setVolume(float volume);
    float volume();

    // Wellenform zur Laufzeit (Waveform-Enum), z. B. aus dem Menü
    void setWaveform(uint8_t waveform);
    uint8_t waveform();
};
