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

// Effekt auf die Mix-Summe (im Menü umschaltbar)
enum FxMode : uint8_t
{
    FX_OFF = 0,
    FX_DELAY,  // Rückkopplungs-Delay (Echo)
    FX_REVERB, // Freeverb-artiger Hall

    FX_COUNT
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

    // Wie noteOn, aber spielt die Note als das angegebene Instrument
    // (Instrument-Enum aus Drums.h) statt als das aktive — der Looper
    // spielt so einen Drum-Loop und Live-Piano gleichzeitig.
    void noteOnAs(uint8_t instrument, uint8_t note, uint8_t velocity);

    // Alle Stimmen ausklingen lassen — z. B. beim Wechsel in den
    // MIDI-Betrieb, damit nichts endlos weiterdudelt
    void allNotesOff();

    // Aftertouch: Lautstärke-Faktor einer klingenden Note (1.0 = wie
    // angeschlagen). Wirkt nur auf gehaltene Stimmen, nicht auf die
    // One-Shots des Drumkits.
    void setPressure(uint8_t note, float factor);

    // Gesamtlautstärke zur Laufzeit (0.0–1.0), z. B. vom Encoder;
    // Startwert ist SPEAKER_MASTER_VOLUME aus der Config
    void setVolume(float volume);
    float volume();

    // Wellenform zur Laufzeit (Waveform-Enum), z. B. aus dem Menü
    void setWaveform(uint8_t waveform);
    uint8_t waveform();

    // Arpeggio-Modus (Index in ARP_STEP_MS; 0 = aus)
    void setArp(uint8_t mode);
    uint8_t arp();

    // Instrument (Instrument-Enum aus Drums.h)
    void setInstrument(uint8_t instrument);
    uint8_t instrument();

    // Effekt auf die Mix-Summe (FxMode-Enum); Umschalten leert die
    // Effekt-Puffer, damit keine alte Fahne stehen bleibt
    void setFx(uint8_t mode);
    uint8_t fx();
};
