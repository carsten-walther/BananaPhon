#include <Arduino.h>

#include "Config.h"

#include "DisplayController.h"
#include "MidiController.h"
#include "TouchSensor.h"

TouchSensor* sensors[NUM_SENSORS];

MidiController midi;

DisplayController displayCtrl;

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
        if (sensors[i]->isPressed())
        {
            midi.noteOff(sensors[i]->note());

            displayCtrl.drawPad(i, false);
        }
    }

    displayCtrl.showCalibrating();

    for (uint8_t i = 0; i < NUM_SENSORS; i++)
    {
        sensors[i]->recalibrate();

        Serial.print("Sensor ");
        Serial.print(i);
        Serial.print(" Baseline: ");
        Serial.println(sensors[i]->baseline());
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

    // Sensoren anlegen und kalibrieren (dabei nicht berühren!)
    for (uint8_t i = 0; i < NUM_SENSORS; i++)
    {
        sensors[i] = new TouchSensor(touchPins[i], midiNotes[i]);
    }

    recalibrateSensors();

    midi.begin();

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
        sensors[i]->update();

        if (sensors[i]->pressedEvent())
        {
            midi.noteOn(sensors[i]->note(), sensors[i]->velocity());

            displayCtrl.drawPad(i, true, sensors[i]->velocity());

            // Tuning-Hilfe für TOUCH_VELOCITY_RATIO_MAX (siehe Config.h)
            Serial.print("NoteOn ");
            Serial.print(sensors[i]->note());
            Serial.print(" vel ");
            Serial.println(sensors[i]->velocity());
        }

        if (sensors[i]->releasedEvent())
        {
            midi.noteOff(sensors[i]->note());

            displayCtrl.drawPad(i, false);
        }
    }

    midi.update();

    // Statuszeile höchstens alle 500 ms prüfen
    if (millis() - lastStatusUpdate > 500)
    {
        lastStatusUpdate = millis();

        displayCtrl.showStatus(midi.bleConnected(), midi.wifiConnected(), midi.rtpReady());
    }

    // Batterieanzeige in größeren Abständen aktualisieren
    if (millis() - lastBatteryUpdate > BATTERY_UPDATE_MS)
    {
        lastBatteryUpdate = millis();

        displayCtrl.showBattery(readBatteryMilliVolts());
    }

    delay(5);
}
