#include "MidiController.h"

#include "Config.h"

#include <WiFi.h>
#include <WiFiManager.h>

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

// WLAN-Setup-Portal (siehe Config.h)
static WiFiManager wifiManager;
static bool portalActive      = false;
static uint32_t wifiStartedAt = 0;

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

        if (WIFI_SSID[0] != '\0')
        {
            // Einkompilierte Zugangsdaten haben Vorrang
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        }
        else
        {
            // Leere SSID: im Flash gespeicherte Daten (Setup-Portal)
            WiFi.begin();
        }

        wifiStartedAt = millis();


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

        // Setup-Portal: kommt binnen WIFI_PORTAL_AFTER_MS keine
        // Verbindung zustande, Access Point mit Captive Portal öffnen.
        // Nicht-blockierend — BLE-MIDI und Pads laufen weiter.
        if (ENABLE_WIFI_PORTAL && !portalActive && WiFi.status() != WL_CONNECTED &&
            millis() - wifiStartedAt > WIFI_PORTAL_AFTER_MS)
        {
            wifiManager.setConfigPortalBlocking(false);

            wifiManager.startConfigPortal(MIDI_DEVICE_NAME);

            portalActive = true;

            Serial.print("WLAN-Setup-Portal aktiv: mit dem WLAN '");
            Serial.print(MIDI_DEVICE_NAME);
            Serial.println("' verbinden und http://192.168.4.1 öffnen.");
        }

        if (portalActive)
        {
            wifiManager.process();

            if (WiFi.status() == WL_CONNECTED)
            {
                portalActive = false;

                Serial.println("Setup-Portal beendet — WLAN konfiguriert.");
            }
        }

        tryStartRTP();
    }


    midiOut.task();
}

void MidiController::noteOn(uint8_t note, uint8_t velocity, uint8_t channel)
{
    midiOut.sendNoteOn(channel, note, velocity);
}

void MidiController::noteOff(uint8_t note, uint8_t channel)
{
    midiOut.sendNoteOff(channel, note, 0);
}

bool MidiController::bleConnected()
{
    return ENABLE_BLE_MIDI && bleMIDI.isConnected();
}

bool MidiController::wifiConnected()
{
    return ENABLE_WIFI_MIDI && WiFi.status() == WL_CONNECTED;
}

bool MidiController::setupPortalActive()
{
    return portalActive;
}

bool MidiController::rtpReady()
{
    return rtpStarted;
}

bool MidiController::usbConnected()
{
    return ENABLE_USB_MIDI && usbMIDI.isConnected();
}
