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
    bool oneShot     = false;
    float pitchDecay = 1.0f; // Tonhöhen-Abfall pro Sample
    float ampDecay   = 1.0f; // Amplituden-Abfall pro Sample
    float toneMix    = 1.0f;
    float noiseMix   = 0.0f;
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

// Phasenschritt für eine MIDI-Note: f = 440 * 2^((note-69)/12)
uint32_t stepForNote(uint8_t note)
{
    float freq = 440.0f * powf(2.0f, (static_cast<int>(note) - 69) / 12.0f);

    return static_cast<uint32_t>(freq / SPEAKER_SAMPLE_RATE * 4294967296.0f);
}

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
                    // Drum: feste Ausklinghüllkurve + fallende Tonhöhe
                    v.amp *= v.ampDecay;

                    if (v.amp < 0.001f)
                    {
                        v.amp    = 0.0f;
                        v.target = 0.0f;

                        continue;
                    }

                    v.step = static_cast<uint32_t>(v.step * v.pitchDecay);

                    v.phase += v.step;

                    float s = 0.0f;

                    if (v.toneMix > 0.0f)
                    {
                        s += sineLut[v.phase >> 22] * v.toneMix;
                    }

                    if (v.noiseMix > 0.0f)
                    {
                        s += noiseSample() * v.noiseMix;
                    }

                    mix += s * v.amp;

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

            // Kopffreiheit: durch die Stimmenzahl teilen, dann Master
            mix *= masterVolume / NUM_SENSORS;

            int32_t s = static_cast<int32_t>(mix);

            if (s > 32767)
            {
                s = 32767;
            }

            if (s < -32768)
            {
                s = -32768;
            }

            // Chiptune: Ausgang auf 256 Stufen rastern (8 Bit) —
            // Hüllkurven und Ausklingen bekommen so das typische
            // "Zipper"-Treppchen der 80er-Soundchips
            if (activeWaveform == WAVE_CHIP)
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

            v.note       = note;
            v.gate       = false; // One-Shot: kein Gate, Arp bleibt inaktiv
            v.oneShot    = true;
            v.phase      = 0;
            v.step       = spec.freq > 0.0f
                               ? static_cast<uint32_t>(spec.freq / SPEAKER_SAMPLE_RATE * 4294967296.0f)
                               : 0;
            v.pitchDecay = spec.pitchDecay;
            v.ampDecay   = spec.ampDecay;
            v.toneMix    = spec.toneMix;
            v.noiseMix   = spec.noiseMix;

            // Schlagstärke direkt setzen — der harte Einsatz gehört
            // zum Drum-Transienten (Sinus startet bei Phase 0)
            v.amp    = velocity / 127.0f;
            v.target = v.amp;

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

            return;
        }
    }

    // Sonst: freie (ausgeklungene) Stimme suchen …
    for (auto& v : voices)
    {
        if (!v.gate && v.amp <= 0.0f)
        // cppcheck-suppress useStlAlgorithm
        {
            v.oneShot = false;

            v.note   = note;
            v.step   = stepForNote(note);
            v.phase  = 0;
            v.target = velocity / 127.0f;
            v.gate   = true;

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

    quietest->oneShot = false;

    quietest->note   = note;
    quietest->step   = stepForNote(note);
    quietest->target = velocity / 127.0f;
    quietest->gate   = true;
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
