#include "SpeakerController.h"

#include <driver/i2s.h>
#include <math.h>

#include "Config.h"

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
};

Voice voices[NUM_SENSORS];

float attackPerSample  = 0.0f;
float releasePerSample = 0.0f;

// Laufzeit-Lautstärke; 32-Bit-Float-Zugriff ist auf dem Xtensa atomar
float masterVolume = SPEAKER_MASTER_VOLUME;

// Aktive Wellenform (Waveform-Enum); 8-Bit-Zugriff ist atomar
uint8_t activeWaveform = WAVE_TRIANGLE;

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

            for (auto& v : voices)
            {
                if (v.amp <= 0.0f && v.target <= 0.0f)
                {
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

                mix += oscSample(v.phase, activeWaveform) * v.amp;
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

    quietest->note   = note;
    quietest->step   = stepForNote(note);
    quietest->target = velocity / 127.0f;
    quietest->gate   = true;
}

void SpeakerController::noteOff(uint8_t note)
{
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
