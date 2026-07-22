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
// klingt es zu brav, hoch auf 500/7.

constexpr uint32_t PIANO_MOD_RATIO = 5; // Modulator = 5-faches der Tonhöhe

constexpr float PIANO_INDEX_START = 350.0f;
constexpr float PIANO_INDEX_FLOOR = 45.0f;

// Zeitkonstante des Anschlags-Glanzes (Abfall des Modulationsindex
// auf den Sockel)
constexpr float PIANO_INDEX_DECAY_MS = 75.0f;

// Amplituden-Hüllkurve: langes Ausklingen bei gehaltener Taste,
// schnelleres Release nach dem Loslassen (jeweils bis -60 dB). Die
// Pro-Sample-Faktoren rechnet der SpeakerController zur Laufzeit aus
// der Abtastrate aus — die Zeiten gelten bei jeder SPEAKER_SAMPLE_RATE.
constexpr float PIANO_DECAY_MS   = 2000.0f;
constexpr float PIANO_RELEASE_MS = 150.0f;

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

// Synthese-Rezept pro Drum. Alle Zeiten in Millisekunden, Filter in
// Hertz — die Pro-Sample-Faktoren rechnet der SpeakerController zur
// Laufzeit aus SPEAKER_SAMPLE_RATE aus. Die Rezepte klingen damit bei
// jeder Abtastrate gleich (nur das Rauschen reicht bei höherer Rate
// weiter hinauf — genau der Zweck von 32 kHz für die HiHats).
struct DrumSpec
{
    float freq;        // Startfrequenz des Ton-Anteils (0 = nur Rauschen)
    float pitchFloor;  // Untergrenze des Sweeps in Hz — OHNE sie liefe
                       // die Tonhöhe exponentiell gegen 0 Hz und die
                       // Drum wäre nach einem Bruchteil ihrer Hüllkurve
                       // unhörbar (die Kick z. B. nach 87 von 320 ms)
    float sweepMs;     // Zeit vom Start bis zum Sockel (0 = kein Sweep)
    float decayMs;     // Ausklingen auf -60 dB (One-Shot)
    float toneMix;     // Anteil Sinus
    float noiseMix;    // Anteil Rauschen
    float noiseCutoff; // Eckfrequenz des Rauschfilters in Hz (ein Pol)
    bool noiseHp;      // true = Hochpass statt Tiefpass. Weißes Rauschen
                       // hat auch nach einem Tiefpass vollen Bassanteil —
                       // HiHats und Clap brauchen die Gegenrichtung,
                       // sonst rauschen sie statt zu zischen.
    uint8_t bursts;    // zusätzliche Anschläge nach dem ersten (0 = keiner).
                       // Ein Clap ist kein einzelner Schlag, sondern eine
                       // Handvoll dicht gestaffelter — erst danach der Tail.
    float burstMs;     // Abstand der Bursts; das schnelle Ausklingen
                       // dazwischen wird daraus abgeleitet
    int8_t chokes;     // Index einer Drum, die dieser Schlag abwürgt
                       // (-1 = keine): die geschlossene HiHat beendet
                       // die offene, wie auf einem echten Kit
    float gain;        // Lautstärke-Ausgleich der Drum gegenüber Melodie
};

// Ausklingen einer abgewürgten Drum. Hart abschalten würde knacken,
// deshalb ein kurzer exponentieller Fade.
constexpr float DRUM_CHOKE_MS = 5.0f;

// Anschlagstärke öffnet den Rausch-Tiefpass: leise Schläge klingen
// dumpfer, harte heller — so verhält sich ein echtes Fell. Der Wert
// ist der Anteil der Eckfrequenz bei Velocity 0 (1.0 = kein Einfluss).
// Bei den hochpassgefilterten Drums bleibt der Filter fest, dort wäre
// die Richtung „heller" nicht eindeutig.
constexpr float DRUM_VEL_TONE_MIN = 0.6f;

// clang-format off
constexpr DrumSpec drumSpecs[NUM_SENSORS] = {
    // freq  floor   sweepMs decayMs  tone   noise  cutHz    hp     brst  brstMs choke gain
    {170.0f,  50.0f,  50.0f, 320.0f, 1.00f, 0.00f,    0.0f, false, 0,     0.0f, -1,  1.7f}, // Kick:        170->50 Hz in 50 ms
    {190.0f, 180.0f,   4.0f, 140.0f, 0.45f, 0.90f, 1250.0f, false, 0,     0.0f, -1,  1.4f}, // Snare:       kurzer Snap, dann fester Ton + dunkles Rauschen
    {  0.0f,   0.0f,   0.0f,  70.0f, 0.00f, 1.00f, 3200.0f, true,  0,     0.0f,  3,  1.5f}, // HiHat zu:    wuergt die offene ab
    {  0.0f,   0.0f,   0.0f, 350.0f, 0.00f, 0.80f, 2800.0f, true,  0,     0.0f, -1,  1.5f}, // HiHat offen
    {105.0f,  80.0f,  60.0f, 250.0f, 1.00f, 0.06f, 1000.0f, false, 0,     0.0f, -1,  1.5f}, // Tom tief:    105->80 Hz
    {160.0f, 120.0f,  60.0f, 250.0f, 1.00f, 0.06f, 1000.0f, false, 0,     0.0f, -1,  1.5f}, // Tom hoch:    160->120 Hz
    {  0.0f,   0.0f,   0.0f, 160.0f, 0.00f, 0.95f, 1250.0f, true,  2,    10.0f, -1,  1.5f}, // Clap:        3 Anschlaege im 10-ms-Raster, dann Tail
};
// clang-format on

constexpr const char* instrumentNames[INST_COUNT] = {"Chip", "Drums", "Piano"};
