#pragma once

#include "Config.h"

#include <Arduino.h>

class MidiController
{
public:
    void begin();

    void update();


    void noteOn(uint8_t note, uint8_t velocity, uint8_t channel = MIDI_CHANNEL);


    void noteOff(uint8_t note, uint8_t channel = MIDI_CHANNEL);


    // Verbindungsstatus (für die Display-Anzeige)

    bool bleConnected();

    bool wifiConnected();

    // WLAN-Setup-Portal (Captive Portal) gerade aktiv?
    bool setupPortalActive();

    bool rtpReady();

    bool usbConnected();
};
