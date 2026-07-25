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

// MIDI-Ausgabe aktiv? Ist sie aus, spielt das BananaPhon unabhängig
// über den Lautsprecher (Standalone), auch wenn ein MIDI-Ziel
// verbunden ist. Umschaltbar im Menü.
bool midi();
void setMidi(bool enabled);

// Effekt am Lautsprecher (FxMode-Enum: Off/Delay/Reverb)
uint8_t fx();
void setFx(uint8_t fx);
} // namespace Settings
