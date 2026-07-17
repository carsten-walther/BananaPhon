#include <Arduino.h>

#include "Config.h"

#include "DisplayController.h"
#include "EncoderController.h"
#include "MenuController.h"
#include "MidiController.h"
#include "Scales.h"
#include "Settings.h"
#include "SpeakerController.h"
#include "TouchSensor.h"

// Statisch statt per new: kein Heap, feste Adressen, cppcheck-freundlich
TouchSensor sensors[NUM_SENSORS];

MidiController midi;

SpeakerController speaker;

EncoderController encoder;

MenuController menu;

// Tatsächlich gespielte Note pro Pad (inkl. Oktav-Shift) — das
// NoteOff muss exakt dieselbe Note treffen, auch wenn die Oktave
// zwischen NoteOn und NoteOff umgestellt wurde.
uint8_t playedNote[NUM_SENSORS] = {0};

// MIDI-Note mit Oktav-Shift, auf den gültigen Bereich begrenzt
static uint8_t shiftedNote(uint8_t i)
{
    int32_t note = scaleNote(Settings::scale(), i) + Settings::octave() * 12;

    if (note < 0)
    {
        note = 0;
    }

    if (note > 127)
    {
        note = 127;
    }

    return static_cast<uint8_t>(note);
}

DisplayController displayCtrl;

// Welche Senke hat das NoteOn bekommen? Das NoteOff muss zur selben —
// sonst hängen Noten, wenn mittendrin ein MIDI-Gerät (dis)connectet.
bool noteViaMidi[NUM_SENSORS] = {false};

bool lastMidiConnected = false;

// MIDI-Ziel vorhanden? Sonst spielt der Lautsprecher (Standalone).
static bool midiConnected()
{
    return midi.bleConnected() || midi.rtpReady();
}

uint32_t lastStatusUpdate  = 0;
uint32_t lastBatteryUpdate = 0;

// Batteriespannung in mV (2:1-Spannungsteiler auf dem Board)
static uint32_t readBatteryMilliVolts()
{
    return analogReadMilliVolts(PIN_BAT_VOLT) * 2;
}

// Kalibriert alle Sensoren neu (Button oder Start). Beendet vorher
// gehaltene Noten — sonst bliebe in der DAW eine Note hängen, weil der
// Sensor-Reset das zugehörige Release-Event verschluckt.
static void recalibrateSensors()
{
    for (uint8_t i = 0; i < NUM_SENSORS; i++)
    {
        if (sensors[i].isPressed())
        {
            if (noteViaMidi[i])
            {
                midi.noteOff(playedNote[i]);
            }
            else
            {
                speaker.noteOff(playedNote[i]);
            }

            displayCtrl.drawPad(i, false);
        }
    }

    displayCtrl.showCalibrating();

    for (uint8_t i = 0; i < NUM_SENSORS; i++)
    {
        sensors[i].recalibrate();

        Serial.print("Sensor ");
        Serial.print(i);
        Serial.print(" Baseline: ");
        Serial.println(sensors[i].baseline());
    }

    displayCtrl.showPads();
}

// ------------------------------------------------
// Setup
// ------------------------------------------------

void setup()
{
    Serial.begin(115200);

    pinMode(PIN_BUTTON_RECALIBRATE, INPUT_PULLUP);

    Settings::begin();

    displayCtrl.begin();

    displayCtrl.setOctave(Settings::octave());

    displayCtrl.setScale(Settings::scale());

    displayCtrl.showCalibrating();

    // Sensoren konfigurieren und kalibrieren (dabei nicht berühren!)
    for (uint8_t i = 0; i < NUM_SENSORS; i++)
    {
        sensors[i].configure(touchPins[i]);
    }

    recalibrateSensors();

    midi.begin();

    speaker.begin();

    speaker.setVolume(Settings::volume());

    speaker.setWaveform(Settings::waveform());

    encoder.begin();

    menu.begin(&speaker, &displayCtrl);

    displayCtrl.showBattery(readBatteryMilliVolts());
}

// ------------------------------------------------
// Loop
// ------------------------------------------------

void loop()
{
    // Rekalibrier-Button (aktiv LOW): löst genau einmal pro Tastendruck
    // aus. Die blockierende Kalibrierung (~1,2 s) wirkt zugleich als
    // Entprellung — beim Rücksprung hierher ist der Taster längst stabil.
    static bool buttonWasPressed = false;

    bool buttonPressed = digitalRead(PIN_BUTTON_RECALIBRATE) == LOW;

    if (buttonPressed && !buttonWasPressed)
    {
        recalibrateSensors();
    }

    buttonWasPressed = buttonPressed;

    for (uint8_t i = 0; i < NUM_SENSORS; i++)
    {
        sensors[i].update();

        if (sensors[i].pressedEvent())
        {
            noteViaMidi[i] = !ENABLE_SPEAKER || midiConnected();

            playedNote[i] = shiftedNote(i);

            if (noteViaMidi[i])
            {
                midi.noteOn(playedNote[i], sensors[i].velocity());
            }
            else
            {
                speaker.noteOn(playedNote[i], sensors[i].velocity());
            }

            displayCtrl.drawPad(i, true, sensors[i].velocity());

            // Tuning-Hilfe für TOUCH_VELOCITY_RATIO_MAX (siehe Config.h)
            Serial.print("NoteOn ");
            Serial.print(playedNote[i]);
            Serial.print(" vel ");
            Serial.println(sensors[i].velocity());
        }

        if (sensors[i].releasedEvent())
        {
            if (noteViaMidi[i])
            {
                midi.noteOff(playedNote[i]);
            }
            else
            {
                speaker.noteOff(playedNote[i]);
            }

            displayCtrl.drawPad(i, false);
        }
    }

    midi.update();

    // Verbindet sich ein MIDI-Ziel, während der Lautsprecher spielt:
    // Stimmen ausklingen lassen, sonst dudeln sie endlos weiter
    if (ENABLE_SPEAKER)
    {
        bool connected = midiConnected();

        if (connected && !lastMidiConnected)
        {
            speaker.allNotesOff();
        }

        lastMidiConnected = connected;
    }

    // Fallende Peak-Marker animieren (intern getaktet)
    displayCtrl.updatePeaks();

    displayCtrl.updateToast();

    // Encoder: Klick öffnet das Settings-Menü bzw. wechselt den
    // Parameter, Drehen ändert den Wert (geschlossen: Lautstärke-
    // Schnellzugriff). Menü-Timeout und NVS-Speichern in update().
    encoder.update();

    if (encoder.clicked())
    {
        menu.handleClick();
    }

    int32_t detents = encoder.readDetents();

    if (detents != 0)
    {
        menu.handleRotation(detents);
    }

    menu.update();

    // Statuszeile höchstens alle 500 ms prüfen
    if (millis() - lastStatusUpdate > 500)
    {
        lastStatusUpdate = millis();

        displayCtrl.showStatus(midi.bleConnected(), midi.wifiConnected(), midi.rtpReady(),
                               midi.setupPortalActive(), ENABLE_SPEAKER && !midiConnected());
    }

    // Batterieanzeige in größeren Abständen aktualisieren
    if (millis() - lastBatteryUpdate > BATTERY_UPDATE_MS)
    {
        lastBatteryUpdate = millis();

        displayCtrl.showBattery(readBatteryMilliVolts());
    }

    delay(5);
}
