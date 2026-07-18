#include "SpeakerController.h"

#include <driver/i2s.h>
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
    float noiseState   = 0.0f;  // Filterzustand (ein Pol)

    // Mehrfach-Anschlag (Clap): so lange burstsLeft > 0 ist, wird die
    // Stimme alle burstSamples neu angeschlagen und klingt dazwischen
    // mit burstDecay ab; danach übernimmt der normale ampDecay-Tail.
    uint8_t burstsLeft      = 0;
    uint32_t burstSamples   = 0;
    uint32_t burstCountdown = 0;
    float burstDecay        = 1.0f;
    float burstAmp          = 0.0f;

    // FM-E-Piano: Modulator-Phase und fallender Modulationsindex
    bool fm           = false;
    uint32_t modPhase = 0;
    float fmIndex     = 0.0f;
};

Voice voices[NUM_SENSORS];

float attackPerSample  = 0.0f;
float releasePerSample = 0.0f;

// Laufzeit-Lautstärke; 32-Bit-Float-Zugriff ist auf dem Xtensa atomar
float masterVolume = SPEAKER_MASTER_VOLUME;

// Aktive Wellenform (Waveform-Enum); 8-Bit-Zugriff ist atomar
uint8_t activeWaveform = WAVE_CHIP;

// Aktives Instrument (Instrument-Enum aus Drums.h)
uint8_t activeInstrument = INST_CHIP;

// 16-Bit-LFSR-Rauschgenerator (NES-Stil) — gemeinsame Quelle für
// alle Drum-Stimmen
uint16_t noiseReg = 0xACE1u;

inline int16_t noiseSample()
{
    uint16_t bit = ((noiseReg >> 0) ^ (noiseReg >> 1)) & 1u;

    noiseReg = static_cast<uint16_t>((noiseReg >> 1) | (bit << 15));

    return (noiseReg & 1u) ? 12000 : -12000;
}

// Arpeggio: Modus, Schrittlänge in Samples (0 = aus). Die Auswahl
// der klingenden Stimme läuft komplett im Audio-Task; der Loop-Task
// setzt nur arpStepSamples (32-Bit-Zugriff atomar).
uint8_t arpMode         = 0;
uint32_t arpStepSamples = 0;

uint8_t arpIndex      = 0;
uint32_t arpCountdown = 0;
bool arpAny           = false;

// De-Klick-Blende pro Stimme beim Arp-Umschalten (~3 ms Rampe)
float arpGain[NUM_SENSORS] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

constexpr float ARP_FADE_PER_SAMPLE = 1.0f / 64.0f;

// Sinus-Tabelle (1024 Einträge, in begin() gefüllt) — direkte
// sinf()-Aufrufe pro Sample wären im Audio-Task zu teuer
int16_t sineLut[1024];

// Frames pro Renderblock (Stereo, 16 Bit) — bei 22,05 kHz sind
// 128 Frames ~5,8 ms Latenz pro Block
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

// Kopffreiheit vor der Sättigung: sieben Melodie-Stimmen können sich
// stapeln, Drums praktisch nie — sie dürfen deshalb viel lauter raus.
// Der Faktor 1.5 gleicht die Kleinsignal-Verstärkung des Soft-Clippers
// aus, damit der Melodie-Pegel derselbe bleibt wie vorher.
constexpr float DRUM_HEADROOM   = 2.5f;
constexpr float MELODY_HEADROOM = NUM_SENSORS * 1.5f;

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

    for (;;)
    {
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

                    for (uint8_t k = 1; k <= NUM_SENSORS; k++)
                    {
                        uint8_t cand = (arpIndex + k) % NUM_SENSORS;

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
                    arpGain[i] += ARP_FADE_PER_SAMPLE;

                    if (arpGain[i] > 1.0f)
                    {
                        arpGain[i] = 1.0f;
                    }
                }
                else if (arpGain[i] > gainTarget)
                {
                    arpGain[i] -= ARP_FADE_PER_SAMPLE;

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

                        // Ein-Pol-Filter auf dem LFSR-Rauschen: als
                        // Tiefpass nimmt er Snare und Toms die blecherne
                        // Härte, als Hochpass (Differenz zum Tiefpass)
                        // gibt er HiHats und Clap ihr Zischen statt
                        // eines breitbandigen Rauschteppichs
                        v.noiseState += (n - v.noiseState) * v.noiseLpf;

                        s += (v.noiseHp ? (n - v.noiseState) : v.noiseState) * v.noiseMix;
                    }

                    mix += s * v.amp * v.gain;

                    continue;
                }

                if (v.fm)
                {
                    // FM-E-Piano: exponentielles Ausklingen — lang bei
                    // gehaltener Taste, schneller nach dem Loslassen
                    v.amp *= v.gate ? PIANO_DECAY : PIANO_RELEASE;

                    if (v.amp < 0.001f)
                    {
                        v.amp    = 0.0f;
                        v.target = 0.0f;

                        continue;
                    }

                    // Anschlags-Glanz: Modulationsindex fällt auf den Sockel
                    v.fmIndex =
                        PIANO_INDEX_FLOOR + (v.fmIndex - PIANO_INDEX_FLOOR) * PIANO_INDEX_DECAY;

                    v.phase += v.step;
                    v.modPhase += v.step * PIANO_MOD_RATIO;

                    // Phasenmodulation: der Modulator verschiebt den
                    // Ablesepunkt in der Sinustabelle des Trägers
                    int32_t offset =
                        static_cast<int32_t>(sineLut[v.modPhase >> 22] * v.fmIndex) / 32000;

                    uint32_t index = ((v.phase >> 22) + offset) & 1023u;

                    mix += sineLut[index] * v.amp * arpGain[gainIndex];

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

                mix += oscSample(v.phase, activeWaveform) * v.amp * arpGain[gainIndex];
            }

            // Auf ±1.0 normieren; Drums bekommen dabei deutlich mehr
            // Pegel als die stapelbaren Melodie-Stimmen
            float x =
                mix * masterVolume /
                ((activeInstrument == INST_DRUMS ? DRUM_HEADROOM : MELODY_HEADROOM) * 32768.0f);

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

// Startet eine Melodie-Stimme (Chip oder FM-Piano) auf `v`
static void startMelodyVoice(Voice& v, uint8_t note, uint8_t velocity)
{
    v.oneShot = false;

    v.note   = note;
    v.step   = stepForNote(note);
    v.phase  = 0;
    v.gate   = true;
    v.target = velocity / 127.0f;

    v.fm = activeInstrument == INST_PIANO;

    if (v.fm)
    {
        // Perkussiver Anschlag: Amplitude sofort setzen (Sinus startet
        // bei Phase 0, daher kein Klick), Anschlags-Glanz aufziehen
        v.amp      = v.target;
        v.modPhase = 0;
        v.fmIndex  = PIANO_INDEX_START;
    }
}

void SpeakerController::noteOn(uint8_t note, uint8_t velocity)
{
    if (activeInstrument == INST_DRUMS)
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

            v.note    = note;
            v.gate    = false; // One-Shot: kein Gate, Arp bleibt inaktiv
            v.oneShot = true;
            v.fm      = false;
            // Choke: dieser Schlag beendet eine andere Drum (geschlossene
            // HiHat würgt die offene ab, wie auf einem echten Kit) —
            // kurzer Fade statt hartem Abschalten, sonst knackt es
            if (spec.chokes >= 0 && spec.chokes < static_cast<int8_t>(NUM_SENSORS))
            {
                Voice& choked = voices[spec.chokes];

                choked.ampDecay   = DRUM_CHOKE_DECAY;
                choked.burstsLeft = 0;
            }

            // Sinus am Maximum starten: der Sprung von 0 auf Vollaus-
            // schlag IST der Anschlagsklick (Beater auf dem Fell). Bei
            // reinen Rausch-Drums bleibt die Phase egal.
            v.phase      = spec.toneMix > 0.0f ? 0x40000000u : 0;
            v.step       = spec.freq > 0.0f ? stepForFreq(spec.freq) : 0;
            v.stepFloor  = spec.pitchFloor > 0.0f ? stepForFreq(spec.pitchFloor) : 0;
            v.pitchDecay = spec.pitchDecay;
            v.ampDecay   = spec.ampDecay;
            v.toneMix    = spec.toneMix;
            v.noiseMix   = spec.noiseMix;
            v.noiseHp    = spec.noiseHp;
            v.gain       = spec.gain;
            v.noiseState = 0.0f;

            // Härter angeschlagene Felle klingen heller: die Schlagstärke
            // öffnet den Rausch-Tiefpass. Beim Hochpass (HiHats, Clap)
            // wäre die Richtung nicht eindeutig — der bleibt fest.
            v.noiseLpf = spec.noiseHp
                             ? spec.noiseLpf
                             : spec.noiseLpf * (DRUM_VEL_TONE_MIN +
                                                (1.0f - DRUM_VEL_TONE_MIN) * velocity / 127.0f);

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

            return;
        }

        return; // unbekannte Note im Drum-Modus: ignorieren
    }

    // Gleiche Note erneut? Dann diese Stimme neu anschlagen.
    for (auto& v : voices)
    {
        if (v.gate && v.note == note)
        // cppcheck-suppress useStlAlgorithm
        {
            v.target = velocity / 127.0f;

            if (v.fm)
            {
                v.amp     = v.target;
                v.fmIndex = PIANO_INDEX_START;
            }

            return;
        }
    }

    // Sonst: freie (ausgeklungene) Stimme suchen …
    for (auto& v : voices)
    {
        if (!v.gate && v.amp <= 0.0f)
        // cppcheck-suppress useStlAlgorithm
        {
            startMelodyVoice(v, note, velocity);

            return;
        }
    }

    // … oder die leiseste Stimme stehlen (bei 7 Pads und 7 Stimmen
    // nur erreichbar, solange Releases noch ausklingen)
    Voice* quietest = &voices[0];

    for (auto& v : voices)
    {
        if (v.amp < quietest->amp)
        {
            // cppcheck-suppress useStlAlgorithm
            quietest = &v;
        }
    }

    startMelodyVoice(*quietest, note, velocity);
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
