#pragma once

#include <Arduino.h>

#include "Config.h"

// ------------------------------------------------
// Skalen
// ------------------------------------------------
//
// Jede Skala bildet die sieben Pads auf Halbtonabstände zum
// Grundton (SCALE_ROOT_NOTE) ab. Pentatonik und Blues haben weniger
// als sieben Stufen pro Oktave — die restlichen Pads setzen die
// Skala in der nächsten Oktave fort.

enum Scale : uint8_t
{
    SCALE_MAJOR = 0, // Dur (ionisch)
    SCALE_MINOR,     // Moll (äolisch)
    SCALE_PENTA,     // Dur-Pentatonik
    SCALE_BLUES,     // Moll-Blues

    SCALE_COUNT
};

constexpr uint8_t scaleIntervals[SCALE_COUNT][NUM_SENSORS] = {
    {0, 2, 4, 5, 7, 9, 11},  // Dur
    {0, 2, 3, 5, 7, 8, 10},  // Moll
    {0, 2, 4, 7, 9, 12, 14}, // Pentatonik
    {0, 3, 5, 6, 7, 10, 12}, // Blues
};

// Anzeigenamen (ASCII, siehe Menü)
constexpr const char* scaleNames[SCALE_COUNT] = {"Major", "Minor", "Penta", "Blues"};

// MIDI-Note eines Pads in der gewählten Skala (ohne Oktav-Shift)
inline uint8_t scaleNote(uint8_t scale, uint8_t index)
{
    return SCALE_ROOT_NOTE + scaleIntervals[scale][index];
}