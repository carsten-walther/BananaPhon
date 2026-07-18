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
    float pitchFloor; // Untergrenze des Sweeps in Hz — OHNE sie liefe die
                      // Tonhöhe exponentiell gegen 0 Hz und die Drum
                      // wäre nach einem Bruchteil ihrer Hüllkurve
                      // unhörbar (die Kick z. B. nach 87 von 320 ms)
    float pitchDecay; // Tonhöhen-Abfall pro Sample (1.0 = keiner)
    float ampDecay;   // Amplituden-Abfall pro Sample (One-Shot)
    float toneMix;    // Anteil Sinus
    float noiseMix;   // Anteil Rauschen
    float noiseLpf;   // Filterkoeffizient fürs Rauschen (ein Pol, pro
                      // Sample): als Tiefpass 1.0 = ungefiltert/hell und
                      // kleiner = dunkler, als Hochpass umgekehrt
    bool noiseHp;     // true = Hochpass statt Tiefpass. Weißes Rauschen
                      // hat auch nach einem Tiefpass vollen Bassanteil —
                      // HiHats und Clap brauchen die Gegenrichtung,
                      // sonst rauschen sie statt zu zischen.
    uint8_t bursts;   // zusätzliche Anschläge nach dem ersten (0 = keiner).
                      // Ein Clap ist kein einzelner Schlag, sondern eine
                      // Handvoll dicht gestaffelter — erst danach der Tail.
    float burstMs;    // Abstand der Bursts; das schnelle Ausklingen
                      // dazwischen wird daraus abgeleitet
    int8_t chokes;    // Index einer Drum, die dieser Schlag abwürgt
                      // (-1 = keine): die geschlossene HiHat beendet
                      // die offene, wie auf einem echten Kit
    float gain;       // Lautstärke-Ausgleich der Drum gegenüber Melodie
};

// Ausklingen einer abgewürgten Drum (~5 ms). Hart abschalten würde
// knacken, deshalb ein kurzer exponentieller Fade.
constexpr float DRUM_CHOKE_DECAY = 0.93926f;

// Anschlagstärke öffnet den Rausch-Tiefpass: leise Schläge klingen
// dumpfer, harte heller — so verhält sich ein echtes Fell. Der Wert
// ist der Filteranteil bei Velocity 0 (1.0 = kein Einfluss). Bei den
// hochpassgefilterten Drums bleibt der Filter fest, dort wäre die
// Richtung „heller" nicht eindeutig.
constexpr float DRUM_VEL_TONE_MIN = 0.6f;

// clang-format off
constexpr DrumSpec drumSpecs[NUM_SENSORS] = {
    // freq  floor   pitchDecay ampDecay  tone   noise  lpf    hp     brst  brstMs choke gain
    {170.0f,  50.0f, 0.99889f, 0.99902f, 1.00f, 0.00f, 1.00f, false, 0,     0.0f, -1,  1.7f}, // Kick:        170->50 Hz in 50 ms, ~320 ms
    {190.0f, 180.0f, 0.99940f, 0.99776f, 0.45f, 0.90f, 0.30f, false, 0,     0.0f, -1,  1.4f}, // Snare:       kurzer Snap, dann fester Ton + dunkles Rauschen
    {  0.0f,   0.0f, 1.00000f, 0.99553f, 0.00f, 1.00f, 0.60f, true,  0,     0.0f,  3,  1.5f}, // HiHat zu:    Hochpass ~3,2 kHz, ~70 ms, wuergt die offene ab
    {  0.0f,   0.0f, 1.00000f, 0.99911f, 0.00f, 0.80f, 0.55f, true,  0,     0.0f, -1,  1.5f}, // HiHat offen: Hochpass ~2,8 kHz, ~350 ms
    {105.0f,  80.0f, 0.99979f, 0.99875f, 1.00f, 0.06f, 0.25f, false, 0,     0.0f, -1,  1.5f}, // Tom tief:    105->80 Hz, ~250 ms
    {160.0f, 120.0f, 0.99979f, 0.99875f, 1.00f, 0.06f, 0.25f, false, 0,     0.0f, -1,  1.5f}, // Tom hoch:    160->120 Hz, ~250 ms
    {  0.0f,   0.0f, 1.00000f, 0.99804f, 0.00f, 0.95f, 0.30f, true,  2,    10.0f, -1,  1.5f}, // Clap:        3 Anschlaege im 10-ms-Raster, dann Tail
};
// clang-format on

constexpr const char* instrumentNames[INST_COUNT] = {"Chip", "Drums", "Piano"};
