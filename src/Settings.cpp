#include "Settings.h"

#include <Preferences.h>

#include "Config.h"
#include "SpeakerController.h"

namespace
{
Preferences prefs;

float vol  = SPEAKER_MASTER_VOLUME;
uint8_t wf = WAVE_TRIANGLE;
int8_t oct = 0;
} // namespace

void Settings::begin()
{
    prefs.begin("bananaphon", false);

    vol = prefs.getFloat("vol", SPEAKER_MASTER_VOLUME);
    wf  = prefs.getUChar("wave", WAVE_TRIANGLE);
    oct = prefs.getChar("oct", 0);

    // Gegen ungültige Altbestände absichern (z. B. nach Firmware-Wechsel)
    if (vol < 0.0f || vol > 1.0f)
    {
        vol = SPEAKER_MASTER_VOLUME;
    }

    if (wf >= WAVE_COUNT)
    {
        wf = WAVE_TRIANGLE;
    }

    if (oct < -OCTAVE_RANGE || oct > OCTAVE_RANGE)
    {
        oct = 0;
    }
}

void Settings::save()
{
    prefs.putFloat("vol", vol);
    prefs.putUChar("wave", wf);
    prefs.putChar("oct", oct);
}

float Settings::volume()
{
    return vol;
}

void Settings::setVolume(float volume)
{
    vol = volume;
}

uint8_t Settings::waveform()
{
    return wf;
}

void Settings::setWaveform(uint8_t waveform)
{
    wf = waveform;
}

int8_t Settings::octave()
{
    return oct;
}

void Settings::setOctave(int8_t octave)
{
    oct = octave;
}
