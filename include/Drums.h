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
    INST_PIANO,    // FM-E-Piano (2-Operator, DX7/Rhodes-Stil)

    INST_COUNT
};

// ------------------------------------------------
// FM-E-Piano
// ------------------------------------------------
//
// Zwei Sinus-Operatoren: der Modulator verschiebt die Phase des
// Trägers. Der Modulationsindex startet hoch (heller, glockiger
// Anschlag) und fällt schnell auf einen Sockel — der Ton wird weich.
// Der Index ist in Sinustabellen-Schritten angegeben (1024 = Periode).

// Klingt das Piano zu metallisch/glockig, senke PIANO_INDEX_START 
// (z. B. auf 300) oder PIANO_MOD_RATIO auf 3 (weicher, orgeliger); 
// klingt es zu brav, hoch auf 500/14.

constexpr uint32_t PIANO_MOD_RATIO = 5; // Modulator = 7-faches der Tonhöhe

constexpr float PIANO_INDEX_START = 350.0f;
constexpr float PIANO_INDEX_FLOOR = 45.0f;
constexpr float PIANO_INDEX_DECAY = 0.99940f; // Anschlags-Glanz, ~75 ms

// Amplituden-Hüllkurve: langes Ausklingen bei gehaltener Taste
// (~2 s auf -60 dB), schnelleres Release nach dem Loslassen (~150 ms)
constexpr float PIANO_DECAY   = 0.999843f;
constexpr float PIANO_RELEASE = 0.99791f;

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
    float noiseLpf;   // Tiefpass fürs Rauschen (1.0 = ungefiltert/hell,
                      // kleiner = dunkler; ein Pol, Koeffizient pro Sample)
    float gain;       // Lautstärke-Ausgleich der Drum gegenüber Melodie
};

constexpr DrumSpec drumSpecs[NUM_SENSORS] = {
    {170.0f, 0.99889f, 0.99902f, 1.0f, 0.00f, 1.0f, 1.7f},  // Kick: 170->50 Hz, ~320 ms
    {190.0f, 0.99960f, 0.99776f, 0.45f, 0.9f, 0.30f, 1.4f}, // Snare: Ton + dunkles Rauschen
    {0.0f, 1.0f, 0.99553f, 0.0f, 1.0f, 0.85f, 1.2f},        // HiHat zu: hell, ~70 ms
    {0.0f, 1.0f, 0.99911f, 0.0f, 0.8f, 0.70f, 1.2f},        // HiHat offen: ~350 ms
    {105.0f, 0.99979f, 0.99875f, 1.0f, 0.06f, 0.25f, 1.5f}, // Tom tief: ~250 ms
    {160.0f, 0.99979f, 0.99875f, 1.0f, 0.06f, 0.25f, 1.5f}, // Tom hoch: ~250 ms
    {0.0f, 1.0f, 0.99804f, 0.0f, 0.95f, 0.40f, 1.3f},       // Clap: mittleres Rauschen
};

constexpr const char* instrumentNames[INST_COUNT] = {"Chip", "Drums", "Piano"};
