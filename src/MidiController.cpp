#include "MidiController.h"

#include "Config.h"

#include <WiFi.h>

#include <ESP32_Host_MIDI.h>
#include <BLEConnection.h>
#include <USBConnection.h>

// Header-only, darf nur in genau einer .cpp eingebunden werden
#include <RTPMIDIConnection.h>


// "midiHandler" ist als Name tabu: die Bibliothek definiert selbst
// ein globales Objekt mit diesem Namen (MIDIHandler.cpp).
static MIDIHandler midiOut;

static BLEConnection bleMIDI;
static RTPMIDIConnection rtpMIDI;
static USBConnection usbMIDI;

static bool rtpStarted = false;

// Startet RTP-MIDI, sobald WLAN verbunden ist (auch nachträglich,
// falls das WLAN beim Boot noch nicht erreichbar war).
static void tryStartRTP()
{
    if (rtpStarted)
    {
        return;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        return;
    }

    if (rtpMIDI.begin(MIDI_DEVICE_NAME))
    {
        midiOut.addTransport(&rtpMIDI);


        rtpStarted = true;


        Serial.print("RTP-MIDI bereit: ");
        Serial.println(WiFi.localIP());
    }
}

void MidiController::begin()
{
    Serial.println("MIDI init");


    midiOut.begin();


    if (ENABLE_BLE_MIDI)
    {
        bleMIDI.begin(MIDI_DEVICE_NAME);

        midiOut.addTransport(&bleMIDI);


        Serial.println("BLE-MIDI bereit");
    }


    if (ENABLE_WIFI_MIDI)
    {
        WiFi.mode(WIFI_STA);

        // Bei WLAN-Abbruch (Router-Neustart, Reichweite) automatisch
        // neu verbinden; persistent(false) verhindert, dass die
        // Zugangsdaten bei jedem Boot erneut in den NVS-Flash
        // geschrieben werden.
        WiFi.persistent(false);

        WiFi.setAutoReconnect(true);

        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);


        Serial.println("WLAN verbindet...");
    }


    if (ENABLE_USB_MIDI)
    {
        usbMIDI.begin();

        midiOut.addTransport(&usbMIDI);


        Serial.println("USB-Host-MIDI bereit");
    }
}

void MidiController::update()
{
    if (ENABLE_WIFI_MIDI)
    {
        // Statusübergänge loggen — Auto-Reconnect übernimmt das
        // Neuverbinden, hier wird es nur sichtbar gemacht. Die
        // RTP-Sockets überleben den Reconnect (gleiche IP per DHCP-
        // Lease); die Gegenstelle nimmt die Session danach wieder auf.
        static bool wasConnected = false;

        bool connected = WiFi.status() == WL_CONNECTED;

        if (connected != wasConnected)
        {
            wasConnected = connected;

            if (connected)
            {
                Serial.print("WLAN verbunden: ");
                Serial.println(WiFi.localIP());
            }
            else
            {
                Serial.println("WLAN getrennt — Auto-Reconnect aktiv");
            }
        }

        tryStartRTP();
    }


    midiOut.task();
}

void MidiController::noteOn(uint8_t note, uint8_t velocity)
{
    midiOut.sendNoteOn(MIDI_CHANNEL, note, velocity);
}

void MidiController::noteOff(uint8_t note)
{
    midiOut.sendNoteOff(MIDI_CHANNEL, note, 0);
}

bool MidiController::bleConnected()
{
    return ENABLE_BLE_MIDI && bleMIDI.isConnected();
}

bool MidiController::wifiConnected()
{
    return ENABLE_WIFI_MIDI && WiFi.status() == WL_CONNECTED;
}

bool MidiController::rtpReady()
{
    return rtpStarted;
}

bool MidiController::usbConnected()
{
    return ENABLE_USB_MIDI && usbMIDI.isConnected();
}
