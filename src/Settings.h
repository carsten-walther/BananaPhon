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

uint8_t scale();
void setScale(uint8_t scale);

uint8_t arp();
void setArp(uint8_t arp);

uint8_t instrument();
void setInstrument(uint8_t instrument);

int8_t octave();
void setOctave(int8_t octave);
} // namespace Settings
