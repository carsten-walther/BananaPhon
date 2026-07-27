#pragma once

#include <Arduino.h>

#include "Config.h"

// ------------------------------------------------
// Instrumente
// ------------------------------------------------

// Reihenfolge = Durchschaltreihenfolge im Menü (Piano, Drums, Synth).
// Achtung: Die Werte sind zugleich die im NVS gespeicherten Indizes —
// wird die Reihenfolge geändert, verschieben sich gespeicherte Werte
// (nach dem Flashen ggf. einmal per Werksreset zurücksetzen).
enum Instrument : uint8_t
{
    INST_PIANO = 0, // FM-E-Piano (2-Operator, DX7/Rhodes-Stil)
    INST_DRUMS,     // Drumkit (One-Shot-Synthese, GM-Percussion)
    INST_CHIP,      // Synth mit wählbarer Wellenform

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

// Anschlagstärke öffnet den Modulationsindex: leise Anschläge klingen
// weicher/dumpfer, harte heller/glockiger — beim echten Klavier wird
// ein fester Hammerschlag nicht nur lauter, sondern auch obertonreicher.
// Der Wert ist der Anteil von PIANO_INDEX_START bei Velocity 0
// (1.0 = kein Einfluss, wie bisher).
constexpr float PIANO_VEL_INDEX_MIN = 0.35f;

// Gegen den „synthetischen" Klang des reinen 2-Operator-FM (perfekt
// harmonische, statische Obertöne):
//
// PIANO_MOD_DETUNE — leichte Inharmonizität: der Modulator läuft nicht
// exakt ganzzahlig, die Obertöne stehen also nicht perfekt harmonisch
// (wie bei echten, leicht gestreckten Klaviersaiten). Anteil des
// Phasenschritts, der zusätzlich addiert wird (0 = aus).
constexpr float PIANO_MOD_DETUNE = 0.02f;

// PIANO_DETUNE / PIANO_CHORUS_MIX — eine zweite, leicht verstimmte
// Trägerstimme sorgt für Schwebung und Breite (Rhodes-artiger Chorus).
// PIANO_DETUNE ist ihre Verstimmung (Anteil der Tonhöhe, ~0.6 % ≈ 10 ct),
// PIANO_CHORUS_MIX ihr Beimischanteil (0 = aus, 0.5 = gleich laut wie
// der Hauptträger; klein halten, sonst wird die Schwebung zum Wobble).
constexpr float PIANO_DETUNE     = 0.006f;
constexpr float PIANO_CHORUS_MIX = 0.15f;

// Amplituden-Hüllkurve: langes Ausklingen bei gehaltener Taste,
// schnelleres Release nach dem Loslassen (jeweils bis -60 dB). Die
// Pro-Sample-Faktoren rechnet der SpeakerController zur Laufzeit aus
// der Abtastrate aus — die Zeiten gelten bei jeder SPEAKER_SAMPLE_RATE.
// PIANO_DECAY_MS ist die Ausklingzeit am Referenzton (siehe unten);
// höhere Töne klingen kürzer aus.
constexpr float PIANO_DECAY_MS   = 2200.0f;
constexpr float PIANO_RELEASE_MS = 150.0f;

// Tonhöhenabhängiges Ausklingen: an einem echten Klavier klingen tiefe
// Saiten sehr lange, hohe nur kurz. Die Ausklingzeit wird deshalb relativ
// zu einem Referenzton skaliert — je PIANO_DECAY_KEY_SEMIS Halbtöne nach
// oben halbiert sie sich (nach unten verdoppelt), begrenzt auf ein
// sinnvolles Fenster. Ohne das klingt jeder Ton gleich lang und dadurch
// mechanisch.
constexpr uint8_t PIANO_DECAY_REF_NOTE = 60;      // c1 (Middle C) = volle Zeit
constexpr float PIANO_DECAY_KEY_SEMIS  = 21.0f;   // Halbtöne bis zur Halbierung
constexpr float PIANO_DECAY_MIN_MS     = 350.0f;  // kürzeste Zeit (hohe Töne)
constexpr float PIANO_DECAY_MAX_MS     = 7000.0f; // längste Zeit (tiefe Töne)

// Zweistufiges Ausklingen: ein angeschlagener Klavierton fällt zuerst
// rasch ab (die „Blüte" direkt nach dem Hammerschlag) und trägt danach
// mit einem langen, leisen Nachklang. Der schnelle Abschnitt gilt,
// solange die Amplitude über PIANO_DECAY_KNEE des Anschlagspegels liegt;
// danach übernimmt das (tonhöhenabhängige) lange Ausklingen. Ein reines
// einstufiges Ausklingen klingt zu gleichförmig/orgelig.
constexpr float PIANO_DECAY_FAST_MS = 600.0f;
constexpr float PIANO_DECAY_KNEE    = 0.5f;

// ------------------------------------------------
// Drumkit
// ------------------------------------------------
//
// Die sieben Pads werden zum Kit. Auf der MIDI-Seite gehen die
// General-MIDI-Percussion-Noten auf Kanal 10 raus — jede DAW spielt
// damit automatisch ein echtes Schlagzeug. Der Speaker synthetisiert
// die Sounds selbst (808-Stil: Sinus mit Pitch-Hüllkurve + weißes
// xorshift-Rauschen).

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
    float noiseCutoff; // Eckfrequenz des Rauschfilters in Hz
    bool noiseHp;      // true = Hochpass (2-polig, 12 dB/Okt) statt
                       // Tiefpass (1-polig). Weißes Rauschen hat auch
                       // nach einem Tiefpass vollen Bassanteil — HiHats
                       // und Clap brauchen die Gegenrichtung, sonst
                       // rauschen sie statt zu zischen; der steile 2-Pol
                       // nimmt ihnen zusätzlich den blechernen Mittenhonk.
    uint8_t bursts;    // zusätzliche Anschläge nach dem ersten (0 = keiner).
                       // Ein Clap ist kein einzelner Schlag, sondern eine
                       // Handvoll dicht gestaffelter — erst danach der Tail.
    float burstMs;     // Abstand der Bursts; das schnelle Ausklingen
                       // dazwischen wird daraus abgeleitet
    int8_t chokes;     // Index einer Drum, die dieser Schlag abwürgt
                       // (-1 = keine): die geschlossene HiHat beendet
                       // die offene, wie auf einem echten Kit
    float gain;        // Lautstärke-Ausgleich der Drum gegenüber Melodie
    float freq2;       // fester zweiter Ton-Teilton in Hz (0 = keiner).
                       // Ein echtes Snare-Fell hat zwei dominante
                       // Resonanzen — der zweite, höhere und nicht
                       // mitschwingende Teilton gibt den typischen „Ring"
                       // statt eines einzelnen, hohlen Tons. Er läuft ohne
                       // Sweep, während der Grundton fällt → leichte
                       // Schwebung.
    float toneMix2;    // Beimischanteil des zweiten Teiltons
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
    // freq  floor   sweepMs decayMs  tone   noise  cutHz    hp     brst  brstMs choke gain    freq2  tone2
    {170.0f,  50.0f,  50.0f, 320.0f, 1.00f, 0.00f,    0.0f, false, 0,     0.0f, -1,  1.7f,     0.0f, 0.00f}, // Kick:        170->50 Hz in 50 ms
    {190.0f, 180.0f,   4.0f, 140.0f, 0.38f, 0.90f, 1250.0f, false, 0,     0.0f, -1,  1.4f,   330.0f, 0.25f}, // Snare:       Snap + zwei Fell-Resonanzen (190/330 Hz) + Rauschen
    {  0.0f,   0.0f,   0.0f,  70.0f, 0.00f, 1.00f, 3200.0f, true,  0,     0.0f,  3,  2.4f,     0.0f, 0.00f}, // HiHat zu:    wuergt die offene ab (Gain: 2-Pol-HP daempft ~1.6x)
    {  0.0f,   0.0f,   0.0f, 350.0f, 0.00f, 0.80f, 2800.0f, true,  0,     0.0f, -1,  2.25f,    0.0f, 0.00f}, // HiHat offen
    {105.0f,  80.0f,  60.0f, 250.0f, 1.00f, 0.06f, 1000.0f, false, 0,     0.0f, -1,  1.5f,     0.0f, 0.00f}, // Tom tief:    105->80 Hz
    {160.0f, 120.0f,  60.0f, 250.0f, 1.00f, 0.06f, 1000.0f, false, 0,     0.0f, -1,  1.5f,     0.0f, 0.00f}, // Tom hoch:    160->120 Hz
    {  0.0f,   0.0f,   0.0f, 160.0f, 0.00f, 0.95f, 1250.0f, true,  2,    10.0f, -1,  1.85f,    0.0f, 0.00f}, // Clap:        3 Anschlaege im 10-ms-Raster, dann Tail (Gain: 2-Pol-HP)
};
// clang-format on

constexpr const char* instrumentNames[INST_COUNT] = {"Piano", "Drums", "Chip"};
