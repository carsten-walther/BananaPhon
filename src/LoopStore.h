#pragma once

#include <Arduino.h>

#include "Config.h"

// ------------------------------------------------
// LoopStore — Loops auf dem Gerät (LittleFS)
// ------------------------------------------------
//
// Speichert und liest Loops als <name>.loop im LittleFS auf der
// spiffs-Partition (3,4 MB). Die mitgelieferten Demo-Loops in data/
// landen per `pio run -t uploadfs` dort; im Betrieb kann der aktuelle
// Loop unter dem nächsten freien Namen gesichert werden. Alles überlebt
// Neustart und Deep Sleep.
//
// Serialisierung übernimmt der LooperController — LoopStore stellt nur
// den gemeinsamen Puffer und die Datei-Ein/Ausgabe.
namespace LoopStore
{
// LittleFS mounten (formatiert bei Mount-Fehler) und Loops scannen.
void begin();

// Anzahl gespeicherter Loops und ihre Namen (ohne .loop) zum Anzeigen.
uint8_t count();
const char* name(uint8_t index);

// Gemeinsamer Puffer für serialize()/deserialize() des Loopers.
uint8_t* buffer();
size_t bufferSize();

// Loop `index` in buffer() lesen; gibt die Länge zurück (0 = Fehler).
// Danach: looper.deserialize(LoopStore::buffer(), len).
size_t read(uint8_t index);

// buffer()[0..len) unter dem nächsten freien Namen (loop1, loop2, …)
// speichern. Vorher mit looper.serialize(buffer(), bufferSize()) füllen.
// Gibt bei Erfolg den vergebenen Namen zurück, sonst nullptr.
const char* saveNext(size_t len);
} // namespace LoopStore
