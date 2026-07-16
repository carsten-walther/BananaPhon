#pragma once

#include <Arduino.h>

class MidiController
{
public:
    void begin();

    void update();


    void noteOn(uint8_t note, uint8_t velocity);


    void noteOff(uint8_t note);


    // Verbindungsstatus (für die Display-Anzeige)

    bool bleConnected();

    bool wifiConnected();

    // WLAN-Setup-Portal (Captive Portal) gerade aktiv?
    bool setupPortalActive();

    bool rtpReady();

    bool usbConnected();
};
