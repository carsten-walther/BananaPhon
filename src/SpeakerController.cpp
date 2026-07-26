#include "SpeakerController.h"

#include <driver/i2s.h>
#include <esp_task_wdt.h>
#include <math.h>

#include "Config.h"

#include "Drums.h"

namespace
{
// Eine Stimme pro Pad. Die Steuerfelder werden vom Loop-Task
// geschrieben und vom Audio-Task gelesen; alle Felder sind 32 Bit
// oder kleiner und damit auf dem Xtensa atomar — schlimmstenfalls
// erwischt der Audio-Task eine Note einen Puffer später.
struct Voice
{
    uint8_t note   = 0;
    bool gate      = false; // Taste gehalten
    uint32_t phase = 0;     // Phasen-Akkumulator (32 Bit = eine Periode)
    uint32_t step  = 0;     // Phasenschritt pro Sample (bestimmt die Tonhöhe)
    float amp      = 0.0f;  // aktuelle Hüllkurven-Amplitude
    float target   = 0.0f;  // Zielamplitude (Velocity bzw. 0 nach NoteOff)

    // One-Shot-Betrieb (Drums): feste Ausklinghüllkurve, NoteOff wird
    // ignoriert, optional fallende Tonhöhe und Rausch-Anteil
    bool oneShot       = false;
    float pitchDecay   = 1.0f; // Tonhöhen-Abfall pro Sample
    uint32_t stepFloor = 0;    // Untergrenze des Sweeps (Phasenschritt)
    float ampDecay     = 1.0f; // Amplituden-Abfall pro Sample
    float toneMix      = 1.0f;
    float noiseMix     = 0.0f;
    float noiseLpf     = 1.0f;  // Filterkoeffizient fürs Rauschen
    bool noiseHp       = false; // Hochpass statt Tiefpass
    float gain         = 1.0f;  // Lautstärke-Ausgleich der Drum
    float noiseState   = 0.0f;  // Filterzustand (erster Pol)
    float noiseState2  = 0.0f;  // zweiter Pol (nur Hochpass-Drums: HiHats/Clap)

    // Mehrfach-Anschlag (Clap): so lange burstsLeft > 0 ist, wird die
    // Stimme alle burstSamples neu angeschlagen und klingt dazwischen
    // mit burstDecay ab; danach übernimmt der normale ampDecay-Tail.
    uint8_t burstsLeft      = 0;
    uint32_t burstSamples   = 0;
    uint32_t burstCountdown = 0;
    float burstDecay        = 1.0f;
    float burstAmp          = 0.0f;

    // Anstehender Retrigger: schlägt dieselbe Drum, während sie noch
    // hörbar klingt, blendet die Stimme erst kurz aus (Choke-Blende)
    // und der Audio-Task schlägt sie danach neu an — ein harter Reset
    // mitten in der Schwingung würde knacken. retrigVel 0 = nichts.
    uint8_t retrigDrum = 0;
    uint8_t retrigVel  = 0;

    // Aftertouch: Lautstärke-Faktor aus dem Anpressdruck (1.0 = wie
    // angeschlagen). Nur für gehaltene Stimmen, One-Shots ignorieren ihn.
    float pressure = 1.0f;

    // FM-E-Piano: Modulator-Phase und fallender Modulationsindex
    bool fm           = false;
    uint32_t modPhase = 0;
    float fmIndex     = 0.0f;

    // Zweiter, leicht verstimmter Träger für Schwebung/Breite (Chorus),
    // damit das Piano weniger statisch-synthetisch klingt
    uint32_t phase2 = 0;
};

// Getrennte Stimmen-Pools: Die Drums belegen feste Stimmen nach
// Pad-Index (0..NUM_SENSORS-1), die Melodie-Stimmen liegen dahinter
// (NUM_SENSORS..NUM_VOICES-1). Ohne die Trennung würde beim Looper eine
// Drum (z. B. die Kick auf jedem Schlag → voices[0]) eine Melodie-Stimme
// mit demselben Index hart überschreiben — das knackte hörbar und kappte
// die Melodie-Note. Getrennt klingen Drum-Loop und Live-Melodie sauber
// nebeneinander (multitimbraler Looper).
constexpr uint8_t MELODY_VOICES = 8;
constexpr uint8_t NUM_VOICES    = NUM_SENSORS + MELODY_VOICES;

Voice voices[NUM_VOICES];

float attackPerSample  = 0.0f;
float releasePerSample = 0.0f;

// Laufzeit-Lautstärke; 32-Bit-Float-Zugriff ist auf dem Xtensa atomar
float masterVolume = SPEAKER_MASTER_VOLUME;

// Aktive Wellenform (Waveform-Enum); 8-Bit-Zugriff ist atomar
uint8_t activeWaveform = WAVE_CHIP;

// Aktives Instrument (Instrument-Enum aus Drums.h)
uint8_t activeInstrument = INST_PIANO;

// Weißes Rauschen per xorshift32 — gemeinsame Quelle für alle Drums.
// Das frühere 1-Bit-LFSR-Rauschen (nur ±12000) hatte den Crest-Faktor
// 1.0, also den maximal harten, „blechernen" Klang eines zufälligen
// Rechtecks. xorshift liefert gleichverteiltes, mehrstufiges Rauschen
// (Crest ~1.7 wie echtes Weißrauschen) — deutlich weicher.
uint32_t noiseReg = 0x1234ABCDu;

inline int16_t noiseSample()
{
    noiseReg ^= noiseReg << 13;
    noiseReg ^= noiseReg >> 17;
    noiseReg ^= noiseReg << 5;

    // Obere 16 Bit als Sample, auf die RMS des alten Rauschens (±12000)
    // skaliert (×0.625), damit die Misch- und Gain-Werte der Drums
    // weitgehend gültig bleiben.
    return static_cast<int16_t>((static_cast<int32_t>(static_cast<int16_t>(noiseReg >> 16)) * 5) >>
                                3);
}

// Arpeggio: Modus, Schrittlänge in Samples (0 = aus). Die Auswahl
// der klingenden Stimme läuft komplett im Audio-Task; der Loop-Task
// setzt nur arpStepSamples (32-Bit-Zugriff atomar).
uint8_t arpMode         = 0;
uint32_t arpStepSamples = 0;

uint8_t arpIndex      = NUM_SENSORS; // zeigt in den Melodie-Pool
uint32_t arpCountdown = 0;
bool arpAny           = false;

// De-Klick-Blende pro Stimme beim Arp-Umschalten (lineare Rampe);
// der Pro-Sample-Schritt wird in begin() aus der Abtastrate berechnet.
// In begin() auf 1.0 gefüllt.
float arpGain[NUM_VOICES];

constexpr float ARP_FADE_MS = 3.0f;

float arpFadePerSample = 1.0f / 64.0f;

// Sinus-Tabelle (1024 Einträge, in begin() gefüllt) — direkte
// sinf()-Aufrufe pro Sample wären im Audio-Task zu teuer
int16_t sineLut[1024];

// Frames pro Renderblock (Stereo, 16 Bit) — die Latenz pro Block ist
// FRAMES / SPEAKER_SAMPLE_RATE (bei 32 kHz: 128 Frames = 4 ms)
constexpr int FRAMES = 128;

// Phasenschritt für eine Frequenz (32 Bit = eine Periode)
uint32_t stepForFreq(float freq)
{
    return static_cast<uint32_t>(freq / SPEAKER_SAMPLE_RATE * 4294967296.0f);
}

// Phasenschritt für eine MIDI-Note: f = 440 * 2^((note-69)/12)
uint32_t stepForNote(uint8_t note)
{
    return stepForFreq(440.0f * powf(2.0f, (static_cast<int>(note) - 69) / 12.0f));
}

// Start-Modulationsindex für einen Anschlag: härtere Velocity macht
// den Anschlags-Glanz heller, nicht nur lauter (siehe PIANO_VEL_INDEX_MIN)
float pianoStartIndex(uint8_t velocity)
{
    return PIANO_INDEX_START *
           (PIANO_VEL_INDEX_MIN + (1.0f - PIANO_VEL_INDEX_MIN) * velocity / 127.0f);
}

// Abklingfaktor pro Sample: erreicht -60 dB (Faktor 0.001) nach ms —
// so bleiben die Hüllkurvenzeiten unabhängig von der Abtastrate
float decayPerSample(float ms)
{
    return ms > 0.0f ? powf(0.001f, 1000.0f / (ms * SPEAKER_SAMPLE_RATE)) : 0.0f;
}

// Ein-Pol-Filterkoeffizient für eine Eckfrequenz in Hz
float onePoleCoeff(float hz)
{
    return 1.0f - expf(-6.2831853f * hz / SPEAKER_SAMPLE_RATE);
}

// Zur Laufzeit aus den ms-Angaben in Drums.h berechnete Faktoren
// (begin() füllt sie, bevor der Audio-Task startet)
float pianoDecay      = 1.0f;
float pianoRelease    = 1.0f;
float pianoIndexDecay = 1.0f;
float drumChokeDecay  = 1.0f;

// ------------------------------------------------
// Effekte (global auf die Mix-Summe, siehe Config.h)
// ------------------------------------------------

// Aktiver Effekt (FxMode); 8-Bit-Zugriff ist atomar. fxReset bittet
// den Audio-Task, die Puffer beim nächsten Block zu leeren — so bleibt
// beim Umschalten keine alte Fahne stehen.
uint8_t activeFx      = FX_OFF;
volatile bool fxReset = false;

// --- Reverb: Freeverb-lite (8 Kamm- + 4 Allpassfilter) ---
// Die Filterlängen sind Freeverbs klassische Stimmung (bei 44,1 kHz),
// auf die eigene Abtastrate skaliert.
constexpr int rvScale(int n)
{
    return n * static_cast<int>(SPEAKER_SAMPLE_RATE) / 44100;
}

constexpr int RV_NCOMB = 8;
constexpr int RV_NAP   = 4;

constexpr int rvCombTune[RV_NCOMB] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr int rvApTune[RV_NAP]     = {556, 441, 341, 225};

constexpr int RV_COMB_MAX = rvScale(1617) + 1;
constexpr int RV_AP_MAX   = rvScale(556) + 1;

// Standard-Freeverb-Skalierung der Config-Parameter
constexpr float rvFeedback = FX_REVERB_ROOM * 0.28f + 0.7f;
constexpr float rvDamp     = FX_REVERB_DAMP * 0.4f;

// Eingangsdämpfung, damit die acht rückgekoppelten Kämme nicht
// übersteuern (klassischer Freeverb-Wert)
constexpr float RV_INPUT_GAIN = 0.015f;

// --- Delay: eine Rückkopplungs-Delay-Line ---
// Pufferlänge = Verzögerungszeit: der Leseindex ist der Schreibindex
// (der Wert dort ist genau DELAY_SAMPLES Samples alt, bevor er
// überschrieben wird).
constexpr int DELAY_SAMPLES = static_cast<int>(FX_DELAY_MS * SPEAKER_SAMPLE_RATE / 1000);

// Delay und Reverb sind nie gleichzeitig aktiv (ein Effekt zur Zeit),
// deshalb teilen sich ihre großen Puffer denselben Speicher — das
// spart den kleineren Effekt komplett. Beim Umschalten leert der
// Audio-Task die Puffer (fxReset), ein Übersprechen ist ausgeschlossen.
union FxPool
{
    float delay[DELAY_SAMPLES];

    struct
    {
        float comb[RV_NCOMB][RV_COMB_MAX];
        float ap[RV_NAP][RV_AP_MAX];
    } rv;
};

FxPool fxPool;

int delayIdx = 0;

int rvCombLen[RV_NCOMB];
int rvCombIdx[RV_NCOMB];
float rvCombStore[RV_NCOMB]; // Tiefpass-Zustand (Dämpfung) je Kamm

int rvApLen[RV_NAP];
int rvApIdx[RV_NAP];

inline float applyDelay(float in)
{
    float d                = fxPool.delay[delayIdx];
    fxPool.delay[delayIdx] = in + d * FX_DELAY_FEEDBACK;

    if (++delayIdx >= DELAY_SAMPLES)
    {
        delayIdx = 0;
    }

    return in + d * FX_DELAY_WET;
}

inline float applyReverb(float in)
{
    float input = in * RV_INPUT_GAIN;
    float out   = 0.0f;

    // Parallele Kammfilter mit Höhendämpfung im Rückkopplungszweig
    for (int i = 0; i < RV_NCOMB; i++)
    {
        float y                         = fxPool.rv.comb[i][rvCombIdx[i]];
        rvCombStore[i]                  = y * (1.0f - rvDamp) + rvCombStore[i] * rvDamp;
        fxPool.rv.comb[i][rvCombIdx[i]] = input + rvCombStore[i] * rvFeedback;

        if (++rvCombIdx[i] >= rvCombLen[i])
        {
            rvCombIdx[i] = 0;
        }

        out += y;
    }

    // In Reihe geschaltete Allpässe streuen die Echos zu einer
    // diffusen Fahne (Feedback fest 0.5)
    for (int i = 0; i < RV_NAP; i++)
    {
        float bufout                = fxPool.rv.ap[i][rvApIdx[i]];
        float y                     = -out + bufout;
        fxPool.rv.ap[i][rvApIdx[i]] = out + bufout * 0.5f;

        if (++rvApIdx[i] >= rvApLen[i])
        {
            rvApIdx[i] = 0;
        }

        out = y;
    }

    return in + out * FX_REVERB_WET;
}

// Leert die geteilten Effekt-Puffer und alle Filterzustände (Aufruf
// aus dem Audio-Task bei fxReset). Nullt den kompletten Union-Speicher
// über beide Sichten — nach dem Umschalten startet der Effekt sauber.
void fxClearBuffers()
{
    delayIdx = 0;

    for (int i = 0; i < DELAY_SAMPLES; i++)
    {
        fxPool.delay[i] = 0.0f;
    }

    for (int i = 0; i < RV_NCOMB; i++)
    {
        rvCombIdx[i]   = 0;
        rvCombStore[i] = 0.0f;

        for (int j = 0; j < RV_COMB_MAX; j++)
        {
            fxPool.rv.comb[i][j] = 0.0f;
        }
    }

    for (int i = 0; i < RV_NAP; i++)
    {
        rvApIdx[i] = 0;

        for (int j = 0; j < RV_AP_MAX; j++)
        {
            fxPool.rv.ap[i][j] = 0.0f;
        }
    }
}

// Schlägt die Drum-Stimme für Pad d an. Wird aus noteOn (Loop-Task)
// gerufen — und aus dem Audio-Task für verzögerte Retrigger, nachdem
// die alte Schwingung ausgeblendet ist (siehe Voice::retrigVel).
void startDrumVoice(Voice& v, uint8_t d, uint8_t velocity)
{
    const DrumSpec& spec = drumSpecs[d];

    v.note    = drumNotes[d];
    v.gate    = false; // One-Shot: kein Gate, Arp bleibt inaktiv
    v.oneShot = true;
    v.fm      = false;

    // Sinus am Nulldurchgang starten. Der Versuch, den Beater-Klick
    // über einen Start im Sinusmaximum zu erzeugen, hat auf dem echten
    // Lautsprecher nur geknackt (Kick und Toms: Sprung von 0 auf
    // Vollausschlag in einem Sample). Der Punch kommt aus dem
    // Pitch-Sweep, nicht aus der Sprungstelle.
    v.phase     = 0;
    v.step      = spec.freq > 0.0f ? stepForFreq(spec.freq) : 0;
    v.stepFloor = spec.pitchFloor > 0.0f ? stepForFreq(spec.pitchFloor) : 0;

    // Sweep-Faktor aus der Zielzeit: erreicht den Sockel nach
    // sweepMs, unabhängig von der Abtastrate
    v.pitchDecay = spec.freq > 0.0f && spec.pitchFloor > 0.0f && spec.sweepMs > 0.0f
                       ? expf(logf(spec.pitchFloor / spec.freq) * 1000.0f /
                              (spec.sweepMs * SPEAKER_SAMPLE_RATE))
                       : 1.0f;

    v.ampDecay    = decayPerSample(spec.decayMs);
    v.toneMix     = spec.toneMix;
    v.noiseMix    = spec.noiseMix;
    v.noiseHp     = spec.noiseHp;
    v.gain        = spec.gain;
    v.noiseState  = 0.0f;
    v.noiseState2 = 0.0f;

    // Härter angeschlagene Felle klingen heller: die Schlagstärke
    // öffnet den Rausch-Tiefpass (skaliert die Eckfrequenz). Beim
    // Hochpass (HiHats, Clap) wäre die Richtung nicht eindeutig —
    // der bleibt fest.
    v.noiseLpf = onePoleCoeff(
        spec.noiseHp ? spec.noiseCutoff
                     : spec.noiseCutoff *
                           (DRUM_VEL_TONE_MIN + (1.0f - DRUM_VEL_TONE_MIN) * velocity / 127.0f));

    // Mehrfach-Anschlag: das Ausklingen zwischen den Bursts wird
    // aus ihrem Abstand abgeleitet (auf 10 % je Intervall)
    v.burstsLeft     = spec.bursts;
    v.burstSamples   = static_cast<uint32_t>(spec.burstMs * 0.001f * SPEAKER_SAMPLE_RATE);
    v.burstCountdown = v.burstSamples;
    v.burstDecay     = spec.bursts > 0 && v.burstSamples > 0
                           ? expf(logf(0.1f) / static_cast<float>(v.burstSamples))
                           : 1.0f;

    // Schlagstärke direkt setzen — der harte Einsatz gehört
    // zum Drum-Transienten
    v.amp      = velocity / 127.0f;
    v.target   = v.amp;
    v.burstAmp = v.amp;

    v.retrigVel = 0;
}

// Kopffreiheit vor der Sättigung: sieben Melodie-Stimmen können sich
// stapeln, Drums praktisch nie — sie dürfen deshalb viel lauter raus.
// Der Faktor 1.5 gleicht die Kleinsignal-Verstärkung des Soft-Clippers
// aus, damit der Melodie-Pegel derselbe bleibt wie vorher.
constexpr float DRUM_HEADROOM   = 2.5f;
constexpr float MELODY_HEADROOM = NUM_SENSORS * 1.5f;

// Die Kopffreiheit wird pro Stimme angewandt (Drum-Stimmen mit dem
// Drum-Faktor, Melodie-Stimmen mit dem Melodie-Faktor) statt global über
// die Summe. Bei einem einzigen Instrument ist das identisch zu vorher;
// beim Looper können Drums und Melodie gleichzeitig klingen und behalten
// jeweils ihren richtigen Pegel.
constexpr float DRUM_GAIN   = 1.0f / DRUM_HEADROOM;
constexpr float MELODY_GAIN = 1.0f / MELODY_HEADROOM;

// Dreieckwelle aus dem Phasen-Akkumulator (weicher als Rechteck)
int16_t triangle(uint32_t phase)
{
    uint16_t x = phase >> 16;

    int32_t s =
        x < 32768 ? static_cast<int32_t>(x) * 2 - 32768 : 98303 - static_cast<int32_t>(x) * 2;

    return static_cast<int16_t>(s);
}

// Ein Sample der aktiven Wellenform. Rechteck etwas leiser (28000
// statt Vollausschlag), weil es subjektiv deutlich lauter wirkt.
int16_t oscSample(uint32_t phase, uint8_t wf)
{
    switch (wf)
    {
    case WAVE_SQUARE:
        return phase < 0x80000000u ? 28000 : -28000;

    case WAVE_SAW:
        return static_cast<int16_t>(static_cast<int32_t>(phase >> 16) - 32768);

    case WAVE_SINE:
        return sineLut[phase >> 22];

    case WAVE_CHIP:
        // 80s-Chiptune (NES-Stil): Pulswelle mit 25 % Tastverhältnis —
        // klingt hohl-nasal statt kantig. Die 8-Bit-Quantisierung
        // passiert in der Mix-Stufe (siehe audioTask), wo sie auf
        // Hüllkurve und Stimmen-Summe wirkt.
        return phase < 0x40000000u ? 26000 : -26000;

    case WAVE_TRIANGLE:
    default:
        return triangle(phase);
    }
}

// Rendert und schreibt Audio-Blöcke — läuft als eigener Task auf
// Core 0, damit Touch/MIDI/Display auf Core 1 unbeeinflusst bleiben.
void audioTask(void*)
{
    static int16_t buf[FRAMES * 2];

    // Diesen Task vom Watchdog überwachen lassen: Die Idle-Überwachung
    // auf CPU0 greift nicht, wenn i2s_write() ewig blockiert (totes I2S),
    // denn dann läuft der Idle-Task ja. Deshalb hier explizit anmelden
    // und pro Block füttern — hängt die Audio-Kette, startet das Gerät neu.
    if (ENABLE_WATCHDOG)
    {
        esp_task_wdt_add(nullptr);
    }

    for (;;)
    {
        if (ENABLE_WATCHDOG)
        {
            esp_task_wdt_reset();
        }

        // Beim Umschalten des Effekts die Puffer leeren, bevor der
        // Block gerendert wird — sonst bliebe eine alte Fahne stehen
        if (fxReset)
        {
            fxClearBuffers();

            fxReset = false;
        }

        for (int f = 0; f < FRAMES; f++)
        {
            float mix = 0.0f;

            // Arpeggio: alle arpStepSamples zur nächsten gehaltenen
            // Stimme weiterschalten (aufsteigend, zyklisch). arpAny
            // merkt sich, ob überhaupt eine Taste gehalten wird —
            // ohne gehaltene Tasten klingen Release-Fahnen normal aus.
            bool arpOn = arpStepSamples > 0;

            if (arpOn)
            {
                if (arpCountdown == 0)
                {
                    arpAny = false;

                    // Nur im Melodie-Pool suchen — Drums haben kein Gate
                    // und werden ohnehin nie arpeggiert
                    for (uint8_t k = 1; k <= MELODY_VOICES; k++)
                    {
                        uint8_t cand = NUM_SENSORS + ((arpIndex - NUM_SENSORS + k) % MELODY_VOICES);

                        if (voices[cand].gate)
                        {
                            arpIndex = cand;
                            arpAny   = true;

                            break;
                        }
                    }

                    arpCountdown = arpStepSamples;
                }

                arpCountdown--;
            }

            uint8_t i = 0;

            for (auto& v : voices)
            {
                // Arp-Blende: aktive Stimme auf 1, alle anderen auf 0 —
                // mit kurzer Rampe gegen Klicks. Ohne Arp (oder ohne
                // gehaltene Tasten) blenden alle Stimmen auf 1 zurück.
                float gainTarget = (!arpOn || !arpAny || i == arpIndex) ? 1.0f : 0.0f;

                if (arpGain[i] < gainTarget)
                {
                    arpGain[i] += arpFadePerSample;

                    if (arpGain[i] > 1.0f)
                    {
                        arpGain[i] = 1.0f;
                    }
                }
                else if (arpGain[i] > gainTarget)
                {
                    arpGain[i] -= arpFadePerSample;

                    if (arpGain[i] < 0.0f)
                    {
                        arpGain[i] = 0.0f;
                    }
                }

                uint8_t gainIndex = i;

                i++;

                if (v.amp <= 0.0f && v.target <= 0.0f)
                {
                    continue;
                }

                if (v.oneShot)
                {
                    // Mehrfach-Anschlag (Clap): vor dem Ausklingen neu
                    // anschlagen, jeder Burst etwas leiser als der davor
                    if (v.burstsLeft > 0)
                    {
                        if (v.burstCountdown == 0)
                        {
                            v.burstAmp *= 0.8f;
                            v.amp            = v.burstAmp;
                            v.burstCountdown = v.burstSamples;

                            v.burstsLeft--;
                        }
                        else
                        {
                            v.burstCountdown--;
                        }
                    }

                    // Drum: feste Ausklinghüllkurve + fallende Tonhöhe.
                    // Während der Bursts klingt es schnell ab, danach
                    // übernimmt der eigentliche Tail.
                    v.amp *= v.burstsLeft > 0 ? v.burstDecay : v.ampDecay;

                    if (v.amp < 0.001f)
                    {
                        v.amp    = 0.0f;
                        v.target = 0.0f;

                        // Verzögerter Retrigger: die Ausblend-Rampe ist
                        // durch, jetzt den anstehenden Schlag starten
                        if (v.retrigVel > 0)
                        {
                            startDrumVoice(v, v.retrigDrum, v.retrigVel);
                        }

                        continue;
                    }

                    // Sweep nur bis zum Sockel — ohne ihn liefe die
                    // Tonhöhe exponentiell gegen 0 Hz, und die Drum
                    // verlöre lange vor dem Ende ihrer Hüllkurve
                    // jeden hörbaren Körper
                    if (v.step > v.stepFloor)
                    {
                        v.step = static_cast<uint32_t>(v.step * v.pitchDecay);

                        if (v.step < v.stepFloor)
                        {
                            v.step = v.stepFloor;
                        }
                    }

                    v.phase += v.step;

                    float s = 0.0f;

                    if (v.toneMix > 0.0f)
                    {
                        s += sineLut[v.phase >> 22] * v.toneMix;
                    }

                    if (v.noiseMix > 0.0f)
                    {
                        int16_t n = noiseSample();

                        v.noiseState += (n - v.noiseState) * v.noiseLpf;

                        float filtered;

                        if (v.noiseHp)
                        {
                            // 2-poliger Hochpass (12 dB/Okt): zwei
                            // kaskadierte Ein-Pol-HP. Der steile Abfall
                            // nimmt HiHats/Clap den blechernen Mittenhonk
                            // und lässt nur das luftige Zischen übrig.
                            float hp1 = n - v.noiseState;

                            v.noiseState2 += (hp1 - v.noiseState2) * v.noiseLpf;

                            filtered = hp1 - v.noiseState2;
                        }
                        else
                        {
                            // Ein-Pol-Tiefpass für Snare/Toms (ein
                            // 2-Pol-Tiefpass machte die Snare zu dumpf)
                            filtered = v.noiseState;
                        }

                        s += filtered * v.noiseMix;
                    }

                    mix += s * v.amp * v.gain * DRUM_GAIN;

                    continue;
                }

                if (v.fm)
                {
                    // FM-E-Piano: exponentielles Ausklingen — lang bei
                    // gehaltener Taste, schneller nach dem Loslassen
                    v.amp *= v.gate ? pianoDecay : pianoRelease;

                    if (v.amp < 0.001f)
                    {
                        v.amp    = 0.0f;
                        v.target = 0.0f;

                        continue;
                    }

                    // Anschlags-Glanz: Modulationsindex fällt auf den Sockel
                    v.fmIndex =
                        PIANO_INDEX_FLOOR + (v.fmIndex - PIANO_INDEX_FLOOR) * pianoIndexDecay;

                    v.phase += v.step;

                    // Zweiter Träger leicht verstimmt (Chorus/Schwebung)
                    v.phase2 += v.step + static_cast<uint32_t>(v.step * PIANO_DETUNE);

                    // Modulator minimal inharmonisch (nicht ganzzahliges
                    // Verhältnis) — Obertöne stehen nicht perfekt harmonisch
                    v.modPhase +=
                        v.step * PIANO_MOD_RATIO + static_cast<uint32_t>(v.step * PIANO_MOD_DETUNE);

                    // Phasenmodulation: der Modulator verschiebt den
                    // Ablesepunkt in der Sinustabelle der Träger
                    int32_t offset =
                        static_cast<int32_t>(sineLut[v.modPhase >> 22] * v.fmIndex) / 32000;

                    uint32_t index  = ((v.phase >> 22) + offset) & 1023u;
                    uint32_t index2 = ((v.phase2 >> 22) + offset) & 1023u;

                    float carrier = sineLut[index] * (1.0f - PIANO_CHORUS_MIX) +
                                    sineLut[index2] * PIANO_CHORUS_MIX;

                    mix += carrier * v.amp * arpGain[gainIndex] * v.pressure * MELODY_GAIN;

                    continue;
                }

                // Lineare Hüllkurve Richtung Zielamplitude
                if (v.amp < v.target)
                {
                    v.amp += attackPerSample;

                    if (v.amp > v.target)
                    {
                        v.amp = v.target;
                    }
                }
                else if (v.amp > v.target)
                {
                    v.amp -= releasePerSample;

                    if (v.amp < 0.0f)
                    {
                        v.amp = 0.0f;
                    }
                }

                v.phase += v.step;

                mix += oscSample(v.phase, activeWaveform) * v.amp * arpGain[gainIndex] *
                       v.pressure * MELODY_GAIN;
            }

            // Auf ±1.0 normieren; die Kopffreiheit steckt schon pro
            // Stimme im Mix (siehe DRUM_GAIN / MELODY_GAIN)
            float x = mix * masterVolume / 32768.0f;

            // Effekt auf die Mix-Summe (vor der Sättigung, damit die
            // nasse Fahne dieselbe Begrenzung bekommt wie das Direktsignal)
            if (activeFx == FX_DELAY)
            {
                x = applyDelay(x);
            }
            else if (activeFx == FX_REVERB)
            {
                x = applyReverb(x);
            }

            // Weiche Sättigung: Spitzen werden gerundet statt hart
            // abgeschnitten. Bei Drums macht das zugleich den Druck,
            // den die Ausgangsstufe eines Drumcomputers erzeugt.
            if (x > 1.0f)
            {
                x = 1.0f;
            }
            else if (x < -1.0f)
            {
                x = -1.0f;
            }
            else
            {
                x = 1.5f * x - 0.5f * x * x * x;
            }

            int32_t s = static_cast<int32_t>(x * 32767.0f);

            // Chiptune: Ausgang auf 256 Stufen rastern (8 Bit) —
            // Hüllkurven und Ausklingen bekommen so das typische
            // "Zipper"-Treppchen der 80er-Soundchips
            // Nur im Chip-Instrument: Drums sollen nicht durch den
            // Bitcrusher (der macht Kick und Rauschen blechern)
            if (activeInstrument == INST_CHIP && activeWaveform == WAVE_CHIP)
            {
                s &= ~0xFF;
            }

            // Gleiches Sample auf beide Kanäle (der MAX98357A mischt
            // bzw. wählt je nach SD-Pin-Beschaltung)
            buf[f * 2]     = static_cast<int16_t>(s);
            buf[f * 2 + 1] = static_cast<int16_t>(s);
        }

        size_t written = 0;

        i2s_write(I2S_NUM_0, buf, sizeof(buf), &written, portMAX_DELAY);
    }
}
} // namespace

void SpeakerController::begin()
{
    if (!ENABLE_SPEAKER)
    {
        return;
    }

    attackPerSample  = 1.0f / (SPEAKER_ATTACK_MS * 0.001f * SPEAKER_SAMPLE_RATE);
    releasePerSample = 1.0f / (SPEAKER_RELEASE_MS * 0.001f * SPEAKER_SAMPLE_RATE);

    // ms-Angaben aus Drums.h in Pro-Sample-Faktoren umrechnen — vor
    // dem Start des Audio-Tasks, der sie liest
    pianoDecay      = decayPerSample(PIANO_DECAY_MS);
    pianoRelease    = decayPerSample(PIANO_RELEASE_MS);
    pianoIndexDecay = expf(-1000.0f / (PIANO_INDEX_DECAY_MS * SPEAKER_SAMPLE_RATE));
    drumChokeDecay  = decayPerSample(DRUM_CHOKE_MS);

    arpFadePerSample = 1000.0f / (ARP_FADE_MS * SPEAKER_SAMPLE_RATE);

    // Reverb-Filterlängen aus der auf die Abtastrate skalierten
    // Freeverb-Stimmung (die Puffer selbst sind static und schon 0)
    for (int i = 0; i < RV_NCOMB; i++)
    {
        rvCombLen[i] = rvScale(rvCombTune[i]);
    }

    for (int i = 0; i < RV_NAP; i++)
    {
        rvApLen[i] = rvScale(rvApTune[i]);
    }

    for (uint8_t i = 0; i < NUM_VOICES; i++)
    {
        arpGain[i] = 1.0f;
    }

    for (int i = 0; i < 1024; i++)
    {
        sineLut[i] = static_cast<int16_t>(sinf(i * 6.2831853f / 1024.0f) * 32000.0f);
    }

    i2s_config_t cfg = {};

    cfg.mode                 = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = SPEAKER_SAMPLE_RATE;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = 0;
    cfg.dma_buf_count        = 4;
    cfg.dma_buf_len          = FRAMES;
    cfg.tx_desc_auto_clear   = true;

    i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);

    i2s_pin_config_t pins = {};

    pins.mck_io_num   = I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = PIN_I2S_BCLK;
    pins.ws_io_num    = PIN_I2S_LRCLK;
    pins.data_out_num = PIN_I2S_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;

    i2s_set_pin(I2S_NUM_0, &pins);

    xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 5, nullptr, 0);

    Serial.println("Lautsprecher bereit (I2S)");
}

// Startet eine Melodie-Stimme (Chip oder FM-Piano) auf `v`. Das
// Instrument wird übergeben (nicht aus activeInstrument gelesen), damit
// der Looper eine Note als ein anderes als das gerade aktive Instrument
// spielen kann (Drums-Loop + Piano-Live gleichzeitig).
static void startMelodyVoice(Voice& v, uint8_t note, uint8_t velocity, uint8_t instrument)
{
    v.oneShot = false;

    v.note   = note;
    v.step   = stepForNote(note);
    v.phase  = 0;
    v.gate   = true;
    v.target = velocity / 127.0f;

    // Aftertouch startet neutral — der Druck moduliert erst ab hier
    v.pressure = 1.0f;

    v.fm = instrument == INST_PIANO;

    if (v.fm)
    {
        // Perkussiver Anschlag: Amplitude sofort setzen (Sinus startet
        // bei Phase 0, daher kein Klick), Anschlags-Glanz aufziehen
        v.amp      = v.target;
        v.modPhase = 0;
        v.phase2   = 0; // zweiter Träger startet in Phase, driftet dann
        v.fmIndex  = pianoStartIndex(velocity);
    }
}

void SpeakerController::noteOn(uint8_t note, uint8_t velocity)
{
    noteOnAs(activeInstrument, note, velocity);
}

void SpeakerController::noteOnAs(uint8_t instrument, uint8_t note, uint8_t velocity)
{
    if (instrument == INST_DRUMS)
    {
        // Drum-Index über die GM-Note finden; feste Stimme pro Drum
        // (Retrigger startet den Sound neu, wie bei einem Drumcomputer)
        for (uint8_t d = 0; d < NUM_SENSORS; d++)
        {
            if (drumNotes[d] != note)
            {
                continue;
            }

            Voice& v = voices[d];

            const DrumSpec& spec = drumSpecs[d];

            // Choke: dieser Schlag beendet eine andere Drum (geschlossene
            // HiHat würgt die offene ab, wie auf einem echten Kit) —
            // kurzer Fade statt hartem Abschalten, sonst knackt es
            if (spec.chokes >= 0 && spec.chokes < static_cast<int8_t>(NUM_SENSORS))
            {
                Voice& choked = voices[spec.chokes];

                choked.ampDecay   = drumChokeDecay;
                choked.burstsLeft = 0;

                // Auch einen anstehenden Retrigger verwerfen — sonst
                // stünde die abgewürgte Drum gleich wieder auf
                choked.retrigVel = 0;
            }

            // Klingt dieselbe Drum noch hörbar, nicht hart hineinsetzen:
            // Phase und Amplitude sprängen mitten in der Schwingung —
            // das Knacken bei schnellen Wiederholungen. Stattdessen per
            // Choke-Blende ausblenden; der Audio-Task schlägt danach
            // neu an (kostet höchstens die ~5 ms der Blende).
            if (v.oneShot && v.amp > 0.01f)
            {
                v.ampDecay   = drumChokeDecay;
                v.burstsLeft = 0;
                v.retrigDrum = d;
                v.retrigVel  = velocity;

                return;
            }

            startDrumVoice(v, d, velocity);

            return;
        }

        return; // unbekannte Note im Drum-Modus: ignorieren
    }

    // Melodie belegt nur den Melodie-Pool (NUM_SENSORS..NUM_VOICES-1),
    // damit Drums (fester Pad-Index) keine Melodie-Stimme überschreiben.

    // Gleiche Note erneut? Dann diese Stimme neu anschlagen.
    for (uint8_t i = NUM_SENSORS; i < NUM_VOICES; i++)
    {
        Voice& v = voices[i];

        if (v.gate && v.note == note)
        {
            v.target = velocity / 127.0f;

            if (v.fm)
            {
                v.amp     = v.target;
                v.fmIndex = pianoStartIndex(velocity);
            }

            return;
        }
    }

    // Sonst: freie (ausgeklungene) Stimme suchen …
    for (uint8_t i = NUM_SENSORS; i < NUM_VOICES; i++)
    {
        if (!voices[i].gate && voices[i].amp <= 0.0f)
        {
            startMelodyVoice(voices[i], note, velocity, instrument);

            return;
        }
    }

    // … oder die leiseste Melodie-Stimme stehlen
    Voice* quietest = &voices[NUM_SENSORS];

    for (uint8_t i = NUM_SENSORS; i < NUM_VOICES; i++)
    {
        if (voices[i].amp < quietest->amp)
        {
            quietest = &voices[i];
        }
    }

    startMelodyVoice(*quietest, note, velocity, instrument);
}

void SpeakerController::noteOff(uint8_t note)
{
    // One-Shots (Drums) klingen ihre feste Hüllkurve aus
    for (auto& v : voices)
    {
        if (v.gate && v.note == note)
        {
            v.gate   = false;
            v.target = 0.0f;
        }
    }
}

void SpeakerController::allNotesOff()
{
    for (auto& v : voices)
    {
        v.gate   = false;
        v.target = 0.0f;
    }
}

void SpeakerController::setPressure(uint8_t note, float factor)
{
    for (auto& v : voices)
    {
        // Nur gehaltene Melodie-Stimmen: One-Shots (Drums) haben kein
        // Gate und klingen ihre feste Hüllkurve aus
        if (v.gate && !v.oneShot && v.note == note)
        // cppcheck-suppress useStlAlgorithm
        {
            v.pressure = factor;

            return;
        }
    }
}

void SpeakerController::setVolume(float volume)
{
    if (volume < 0.0f)
    {
        volume = 0.0f;
    }

    if (volume > 1.0f)
    {
        volume = 1.0f;
    }

    masterVolume = volume;
}

float SpeakerController::volume()
{
    return masterVolume;
}

void SpeakerController::setWaveform(uint8_t waveform)
{
    if (waveform >= WAVE_COUNT)
    {
        waveform = WAVE_TRIANGLE;
    }

    activeWaveform = waveform;
}

uint8_t SpeakerController::waveform()
{
    return activeWaveform;
}

void SpeakerController::setArp(uint8_t mode)
{
    if (mode >= ARP_MODE_COUNT)
    {
        mode = 0;
    }

    arpMode = mode;

    arpStepSamples = ARP_STEP_MS[mode] * SPEAKER_SAMPLE_RATE / 1000;
}

uint8_t SpeakerController::arp()
{
    return arpMode;
}

void SpeakerController::setInstrument(uint8_t instrument)
{
    if (instrument >= INST_COUNT)
    {
        instrument = INST_CHIP;
    }

    activeInstrument = instrument;
}

uint8_t SpeakerController::instrument()
{
    return activeInstrument;
}

void SpeakerController::setFx(uint8_t mode)
{
    if (mode >= FX_COUNT)
    {
        mode = FX_OFF;
    }

    if (mode != activeFx)
    {
        activeFx = mode;

        // Puffer beim nächsten Block leeren (im Audio-Task), damit die
        // alte Fahne nicht in den neuen Effekt hineinklingt
        fxReset = true;
    }
}

uint8_t SpeakerController::fx()
{
    return activeFx;
}
