#pragma once

#include <Arduino.h>

#include "Config.h"

// ------------------------------------------------
// Instrumente
// ------------------------------------------------

// Neue Instrumente werden hinten angehängt, damit gespeicherte
// NVS-Werte gültig bleiben.
enum Instrument : uint8_t
{
    INST_CHIP = 0, // Synth mit wählbarer Wellenform
    INST_DRUMS,    // Drumkit (One-Shot-Synthese, GM-Percussion)

    INST_COUNT
};

// ------------------------------------------------
// Drumkit
// ------------------------------------------------
//
// Die sieben Pads werden zum Kit. Auf der MIDI-Seite gehen die
// General-MIDI-Percussion-Noten auf Kanal 10 raus — jede DAW spielt
// damit automatisch ein echtes Schlagzeug. Der Speaker synthetisiert
// die Sounds selbst (808-Stil: Sinus mit Pitch-Hüllkurve + LFSR-
// Rauschen).

constexpr uint8_t DRUM_MIDI_CHANNEL = 10;

// GM: Kick, Snare, HiHat zu, HiHat offen, Tom tief, Tom hoch, Clap
constexpr uint8_t drumNotes[NUM_SENSORS] = {36, 38, 42, 46, 45, 50, 39};

// Kürzel für die Tastenbeschriftung
constexpr const char* drumLabels[NUM_SENSORS] = {"KD", "SN", "HH", "OH", "T1", "T2", "CP"};

// Synthese-Rezept pro Drum. Die Decay-Faktoren gelten pro Sample bei
// SPEAKER_SAMPLE_RATE (22,05 kHz): f = exp(ln(0.001) / (22.05 * ms))
// für ein Ausklingen auf -60 dB in der angegebenen Zeit.
struct DrumSpec
{
    float freq;       // Startfrequenz des Ton-Anteils (0 = nur Rauschen)
    float pitchDecay; // Tonhöhen-Abfall pro Sample (1.0 = keiner)
    float ampDecay;   // Amplituden-Abfall pro Sample (One-Shot)
    float toneMix;    // Anteil Sinus
    float noiseMix;   // Anteil Rauschen
};

constexpr DrumSpec drumSpecs[NUM_SENSORS] = {
    {150.0f, 0.99917f, 0.99875f, 1.0f, 0.05f}, // Kick: 150->50 Hz, ~250 ms
    {180.0f, 0.99950f, 0.99739f, 0.4f, 0.80f}, // Snare: Ton-Burst + Rauschen, ~120 ms
    {0.0f, 1.0f, 0.99479f, 0.0f, 1.00f},       // HiHat zu: kurzes Rauschen, ~60 ms
    {0.0f, 1.0f, 0.99922f, 0.0f, 0.80f},       // HiHat offen: ~400 ms
    {110.0f, 0.99974f, 0.99844f, 1.0f, 0.10f}, // Tom tief: ~200 ms
    {170.0f, 0.99974f, 0.99844f, 1.0f, 0.10f}, // Tom hoch: ~200 ms
    {0.0f, 1.0f, 0.99791f, 0.0f, 0.90f},       // Clap (vereinfacht): ~150 ms
};

constexpr const char* instrumentNames[INST_COUNT] = {"Chip", "Drums"};
