#include "Settings.h"

#include <Preferences.h>

#include "Config.h"
#include "Drums.h"
#include "Scales.h"
#include "SpeakerController.h"

namespace
{
Preferences prefs;

float vol    = SPEAKER_MASTER_VOLUME;
uint8_t wf   = WAVE_CHIP;
uint8_t sc   = SCALE_MAJOR;
uint8_t arpM = 0;
uint8_t inst = INST_PIANO;
int8_t oct   = 0;
bool midiOn  = false;  // MIDI-Ausgabe standardmäßig aus (Standalone)
uint8_t fxM  = FX_OFF; // Effekt am Lautsprecher, standardmäßig aus
} // namespace

void Settings::begin()
{
    prefs.begin("bananaphon", false);

    vol    = prefs.getFloat("vol", SPEAKER_MASTER_VOLUME);
    wf     = prefs.getUChar("wave", WAVE_CHIP);
    sc     = prefs.getUChar("scale", SCALE_MAJOR);
    arpM   = prefs.getUChar("arp", 0);
    inst   = prefs.getUChar("inst", INST_PIANO);
    oct    = prefs.getChar("oct", 0);
    midiOn = prefs.getBool("midi", false);
    fxM    = prefs.getUChar("fx", FX_OFF);

    // Gegen ungültige Altbestände absichern (z. B. nach Firmware-Wechsel)
    if (vol < 0.0f || vol > 1.0f)
    {
        vol = SPEAKER_MASTER_VOLUME;
    }

    if (wf >= WAVE_COUNT)
    {
        wf = WAVE_CHIP;
    }

    if (sc >= SCALE_COUNT)
    {
        sc = SCALE_MAJOR;
    }

    if (arpM >= ARP_MODE_COUNT)
    {
        arpM = 0;
    }

    if (inst >= INST_COUNT)
    {
        inst = INST_PIANO;
    }

    if (oct < -OCTAVE_RANGE || oct > OCTAVE_RANGE)
    {
        oct = 0;
    }

    if (fxM >= FX_COUNT)
    {
        fxM = FX_OFF;
    }
}

void Settings::save()
{
    prefs.putFloat("vol", vol);
    prefs.putUChar("wave", wf);
    prefs.putUChar("scale", sc);
    prefs.putUChar("arp", arpM);
    prefs.putUChar("inst", inst);
    prefs.putChar("oct", oct);
    prefs.putBool("midi", midiOn);
    prefs.putUChar("fx", fxM);
}

void Settings::factoryReset()
{
    // Alle Schlüssel im Namespace löschen; nach einem Neustart liefert
    // begin() für jeden Wert wieder den Default aus der Config.h
    prefs.clear();
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

uint8_t Settings::scale()
{
    return sc;
}

void Settings::setScale(uint8_t scale)
{
    sc = scale;
}

uint8_t Settings::arp()
{
    return arpM;
}

void Settings::setArp(uint8_t arp)
{
    arpM = arp;
}

uint8_t Settings::instrument()
{
    return inst;
}

void Settings::setInstrument(uint8_t instrument)
{
    inst = instrument;
}

int8_t Settings::octave()
{
    return oct;
}

void Settings::setOctave(int8_t octave)
{
    oct = octave;
}

bool Settings::midi()
{
    return midiOn;
}

void Settings::setMidi(bool enabled)
{
    midiOn = enabled;
}

uint8_t Settings::fx()
{
    return fxM;
}

void Settings::setFx(uint8_t fx)
{
    fxM = fx;
}
