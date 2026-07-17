#include <Arduino.h>

#include "Config.h"

#include "DisplayController.h"
#include "EncoderController.h"
#include "MidiController.h"
#include "SpeakerController.h"
#include "TouchSensor.h"

// Statisch statt per new: kein Heap, feste Adressen, cppcheck-freundlich
TouchSensor sensors[NUM_SENSORS];

MidiController midi;

SpeakerController speaker;

EncoderController encoder;

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
                midi.noteOff(sensors[i].note());
            }
            else
            {
                speaker.noteOff(sensors[i].note());
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

    displayCtrl.begin();

    displayCtrl.showCalibrating();

    // Sensoren konfigurieren und kalibrieren (dabei nicht berühren!)
    for (uint8_t i = 0; i < NUM_SENSORS; i++)
    {
        sensors[i].configure(touchPins[i], midiNotes[i]);
    }

    recalibrateSensors();

    midi.begin();

    speaker.begin();

    encoder.begin();

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

            if (noteViaMidi[i])
            {
                midi.noteOn(sensors[i].note(), sensors[i].velocity());
            }
            else
            {
                speaker.noteOn(sensors[i].note(), sensors[i].velocity());
            }

            displayCtrl.drawPad(i, true, sensors[i].velocity());

            // Tuning-Hilfe für TOUCH_VELOCITY_RATIO_MAX (siehe Config.h)
            Serial.print("NoteOn ");
            Serial.print(sensors[i].note());
            Serial.print(" vel ");
            Serial.println(sensors[i].velocity());
        }

        if (sensors[i].releasedEvent())
        {
            if (noteViaMidi[i])
            {
                midi.noteOff(sensors[i].note());
            }
            else
            {
                speaker.noteOff(sensors[i].note());
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

    // Encoder: im Standalone-Betrieb regelt Drehen die Lautstärke,
    // ein Druck zeigt den aktuellen Wert an
    encoder.update();

    int32_t detents = encoder.readDetents();

    bool standalone = ENABLE_SPEAKER && !midiConnected();

    if (standalone && (detents != 0 || encoder.clicked()))
    {
        if (detents != 0)
        {
            speaker.setVolume(speaker.volume() + detents * ENCODER_VOLUME_STEP);
        }

        char toast[16];

        snprintf(toast, sizeof(toast), "Volume %d%%",
                 static_cast<int>(speaker.volume() * 100.0f + 0.5f));

        displayCtrl.showToast(toast);
    }

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
