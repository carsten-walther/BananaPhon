#pragma once

#include <Arduino.h>

// Persistente Einstellungen im NVS-Flash (Preferences-Library).
// Werte überleben Neustarts; Defaults kommen aus der Config.h.
namespace Settings
{
void begin(); // aus dem NVS laden
void save();  // in den NVS schreiben (nur bei Bedarf aufrufen)

float volume();
void setVolume(float volume);

uint8_t waveform();
void setWaveform(uint8_t waveform);

int8_t octave();
void setOctave(int8_t octave);
} // namespace Settings
