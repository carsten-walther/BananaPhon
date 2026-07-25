#include <Arduino.h>

#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "Config.h"

#include "DisplayController.h"
#include "EncoderController.h"
#include "MenuController.h"
#include "MidiController.h"
#include "OtaController.h"
#include "Scales.h"
#include "Drums.h"
#include "Settings.h"
#include "SpeakerController.h"
#include "TouchSensor.h"

// Statisch statt per new: kein Heap, feste Adressen, cppcheck-freundlich
TouchSensor sensors[NUM_SENSORS];

MidiController midi;

SpeakerController speaker;

EncoderController encoder;

MenuController menu;

OtaController ota;

// Tatsächlich gespielte Note pro Pad (inkl. Oktav-Shift) — das
// NoteOff muss exakt dieselbe Note treffen, auch wenn die Oktave
// zwischen NoteOn und NoteOff umgestellt wurde.
uint8_t playedNote[NUM_SENSORS] = {0};

// Kanal des NoteOn (Drums: Kanal 10) — das NoteOff folgt ihm
uint8_t playedChannel[NUM_SENSORS] = {0};

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

// Gehen Noten per MIDI raus? Nur wenn die MIDI-Ausgabe im Menü aktiv
// ist UND ein Ziel verbunden ist. Ist der Schalter aus, spielt der
// Lautsprecher unabhängig vom Verbindungsstatus (Standalone) — so lässt
// sich das BananaPhon auch ohne DAW als Instrument benutzen.
static bool midiActive()
{
    return Settings::midi() && (midi.bleConnected() || midi.rtpReady());
}

uint32_t lastStatusUpdate  = 0;
uint32_t lastBatteryUpdate = 0;

// Deep Sleep: Zeitpunkt der letzten Bedienung. Läuft
// DEEP_SLEEP_TIMEOUT_MS ohne Aktivität ab, schläft das Gerät ein.
uint32_t lastActivity = 0;

// Aftertouch: Sendetakt und zuletzt gesendeter Wert. Den Bezugspunkt
// für die Modulation hält jeder TouchSensor selbst (pressureDelta).
uint32_t lastAftertouch  = 0;
uint8_t lastSentPressure = 0;
bool aftertouchSent      = false;

// Batteriespannung in mV (2:1-Spannungsteiler auf dem Board)
static uint32_t readBatteryMilliVolts()
{
    return analogReadMilliVolts(PIN_BAT_VOLT) * 2;
}

// Kalibriert alle Sensoren neu (Menüpunkt oder Start). Beendet vorher
// gehaltene Noten — sonst bliebe in der DAW eine Note hängen, weil der
// Sensor-Reset das zugehörige Release-Event verschluckt.
static void recalibrateSensors(bool showUi = true)
{
    for (uint8_t i = 0; i < NUM_SENSORS; i++)
    {
        if (sensors[i].isPressed())
        {
            if (noteViaMidi[i])
            {
                midi.noteOff(playedNote[i], playedChannel[i]);
            }
            else
            {
                speaker.noteOff(playedNote[i]);
            }

            displayCtrl.drawPad(i, false);
        }
    }

    if (showUi)
    {
        displayCtrl.showCalibrating();
    }

    for (uint8_t i = 0; i < NUM_SENSORS; i++)
    {
        sensors[i].recalibrate();

        Serial.print("Sensor ");
        Serial.print(i);
        Serial.print(" Baseline: ");
        Serial.println(sensors[i].baseline());
    }

    if (showUi)
    {
        displayCtrl.showPads();
    }
}

// ------------------------------------------------
// Aftertouch
// ------------------------------------------------

// Druckänderungen bei gehaltenen Noten auswerten: am Lautsprecher als
// Lautstärke-Modulation der einzelnen Stimme, per MIDI als Channel
// Pressure. Im Drumkit wirkungslos — One-Shots haben nichts, das sich
// während des Haltens noch modulieren ließe.
static void updateAftertouch()
{
    if (Settings::instrument() == INST_DRUMS)
    {
        return;
    }

    uint8_t midiMax = 0;
    bool anyMidi    = false;

    for (uint8_t i = 0; i < NUM_SENSORS; i++)
    {
        if (!sensors[i].isPressed())
        {
            continue;
        }

        uint8_t p = sensors[i].pressure();

        if (noteViaMidi[i])
        {
            // Channel Pressure gilt für den ganzen Kanal, nicht pro
            // Note — der stärkste gehaltene Finger bestimmt den Wert
            if (!anyMidi || p > midiMax)
            {
                midiMax = p;
            }

            anyMidi = true;

            continue;
        }

        // Lautsprecher: Faktor relativ zum eingeschwungenen Griff,
        // damit die Anschlagsdynamik erhalten bleibt und der Druck nur
        // die Änderung ausdrückt (ohne Nachdrücken also genau 1.0)
        int32_t delta = sensors[i].pressureDelta();

        float factor = delta >= 0 ? 1.0f + (delta / 127.0f) * (AFTERTOUCH_SPEAKER_MAX - 1.0f)
                                  : 1.0f + (delta / 127.0f) * (1.0f - AFTERTOUCH_SPEAKER_MIN);

        if (factor < AFTERTOUCH_SPEAKER_MIN)
        {
            factor = AFTERTOUCH_SPEAKER_MIN;
        }

        if (factor > AFTERTOUCH_SPEAKER_MAX)
        {
            factor = AFTERTOUCH_SPEAKER_MAX;
        }

        speaker.setPressure(playedNote[i], factor);

        // Debug: Druckverlauf pro Pad, gedrosselt über denselben
        // Deadband wie der MIDI-Versand — zum Einstellen der Kennlinie
        static uint8_t lastLogged[NUM_SENSORS] = {0};

        if (abs(static_cast<int>(p) - static_cast<int>(lastLogged[i])) >= AFTERTOUCH_DEADBAND)
        {
            lastLogged[i] = p;

            Serial.print("Druck Pad ");
            Serial.print(i);
            Serial.print(": ");
            Serial.print(p);
            Serial.print(" (Delta ");
            Serial.print(delta);
            Serial.print(", Faktor ");
            Serial.print(factor, 2);
            Serial.println(")");
        }
    }

    if (!anyMidi)
    {
        // Letzte per MIDI gehaltene Note ist weg: Druck einmal auf 0
        // zurücknehmen, sonst bleibt der Klangerzeuger am anderen Ende
        // auf dem zuletzt gesendeten Wert stehen
        if (aftertouchSent)
        {
            midi.channelPressure(0);

            aftertouchSent   = false;
            lastSentPressure = 0;
        }

        return;
    }

    // Nur bei nennenswerter Änderung senden — sonst flutet der
    // Druckwert den MIDI-Bus (siehe AFTERTOUCH_DEADBAND)
    if (aftertouchSent &&
        abs(static_cast<int>(midiMax) - static_cast<int>(lastSentPressure)) < AFTERTOUCH_DEADBAND)
    {
        return;
    }

    midi.channelPressure(midiMax);

    lastSentPressure = midiMax;
    aftertouchSent   = true;

    Serial.print("Aftertouch ");
    Serial.println(midiMax);
}

// ------------------------------------------------
// Deep Sleep
// ------------------------------------------------

// Legt das Gerät schlafen (siehe Config.h). Kehrt nicht zurück — der
// Deep Sleep ist praktisch ein Reset, der Rekalibrier-Button (aktiv
// LOW) weckt per ext1 wieder auf, danach läuft setup() normal neu.
static void enterDeepSleep()
{
    Serial.println("Deep Sleep — Aufwecken durch Drehen des Encoders");

    Serial.flush();

    // Ton aus (Lautsprecher-Fahnen und – falls doch etwas hängt –
    // per MIDI gehaltene Noten). Beim Einschlafen ist nichts gedrückt,
    // die Schleife ist im Normalfall ein No-Op.
    speaker.allNotesOff();

    for (uint8_t i = 0; i < NUM_SENSORS; i++)
    {
        if (sensors[i].isPressed() && noteViaMidi[i])
        {
            midi.noteOff(playedNote[i], playedChannel[i]);
        }
    }

    displayCtrl.showSleep();

    delay(1200);

    displayCtrl.powerOff();

    // Wecken über die Encoder-Spur B (RTC-fähig, ext1). Der Taster
    // GPIO43 kann den S3 nicht aus dem Deep Sleep wecken (nicht
    // RTC-fähig), Spur B schon. Um unabhängig von der Rasterstellung zu
    // wecken, den aktuellen Ruhepegel lesen und auf den GEGENpegel
    // triggern — so weckt jede Drehung, ohne sofort wieder einzuschlafen.
    // RTC-Pullup halten (Encoder ruht per Pullup auf HIGH), RTC_PERIPH
    // dafür versorgt lassen.
    gpio_num_t wakePin = static_cast<gpio_num_t>(PIN_ENCODER_B);

    rtc_gpio_pullup_en(wakePin);
    rtc_gpio_pulldown_dis(wakePin);

    esp_sleep_ext1_wakeup_mode_t mode =
        digitalRead(PIN_ENCODER_B) == HIGH ? ESP_EXT1_WAKEUP_ANY_LOW : ESP_EXT1_WAKEUP_ANY_HIGH;

    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    esp_sleep_enable_ext1_wakeup(1ULL << PIN_ENCODER_B, mode);

    esp_deep_sleep_start();
}

// ------------------------------------------------
// Setup
// ------------------------------------------------

void setup()
{
    Serial.begin(115200);

    Settings::begin();

    displayCtrl.begin();

    displayCtrl.setOctave(Settings::octave());

    displayCtrl.setScale(Settings::scale());

    displayCtrl.setInstrument(Settings::instrument());

    // Splash-Screen: Name + Version; währenddessen laufen Touch-
    // Kalibrierung und Funk-Initialisierung im Hintergrund
    uint32_t splashStart = millis();

    displayCtrl.showSplash();

    // Sensoren konfigurieren und kalibrieren (dabei nicht berühren!)
    for (uint8_t i = 0; i < NUM_SENSORS; i++)
    {
        sensors[i].configure(touchPins[i]);
    }

    recalibrateSensors(false);

    midi.begin();

    speaker.begin();

    speaker.setVolume(Settings::volume());

    speaker.setWaveform(Settings::waveform());

    speaker.setArp(Settings::arp());

    speaker.setInstrument(Settings::instrument());

    speaker.setFx(Settings::fx());

    encoder.begin();

    menu.begin(&speaker, &displayCtrl);

    // OTA: Während eines Updates den Ton stoppen und den Fortschritt
    // auf dem Display zeigen. Die Callbacks laufen im (blockierenden)
    // Update-Kontext, nicht aus loop() — nur von dort ist der Verlauf
    // sichtbar. Die eigentlichen Dienste startet ota.update() erst,
    // sobald das WLAN verbunden ist.
    if (ENABLE_OTA)
    {
        ota.onStart(
            []
            {
                speaker.allNotesOff();
                displayCtrl.showOtaScreen("Bitte warten - nicht ausschalten", -1);
            });

        ota.onProgress([](uint8_t percent) { displayCtrl.showOtaScreen(nullptr, percent); });

        // RGB565 direkt (die TFT_*-Makros gehören dem DisplayController):
        // 0x07E0 = Grün, 0xF800 = Rot
        ota.onEnd([] { displayCtrl.showOtaScreen("Fertig - Neustart ...", -1, 0x07E0); });

        ota.onError([] { displayCtrl.showOtaScreen("Fehler - Abbruch", -1, 0xF800); });

        ota.begin();
    }

    // Splash mindestens SPLASH_MS stehen lassen, dann die normale
    // Oberfläche aufbauen
    while (millis() - splashStart < SPLASH_MS)
    {
        delay(10);
    }

    displayCtrl.showHome();

    displayCtrl.showBattery(readBatteryMilliVolts());

    // Inaktivitäts-Timer erst jetzt starten (nach Splash/Init)
    lastActivity = millis();
}

// ------------------------------------------------
// Loop
// ------------------------------------------------

void loop()
{
    // Bedienung für den Deep-Sleep-Timer sammeln (Pads, Encoder)
    bool activity = false;

    for (uint8_t i = 0; i < NUM_SENSORS; i++)
    {
        sensors[i].update();

        // Gedrücktes oder gehaltenes Pad zählt als Aktivität — so
        // schläft das Gerät auch bei langer gehaltener Note nicht ein
        if (sensors[i].isPressed())
        {
            activity = true;
        }

        if (sensors[i].pressedEvent())
        {
            noteViaMidi[i] = !ENABLE_SPEAKER || midiActive();

            bool drums = Settings::instrument() == INST_DRUMS;

            playedNote[i]    = drums ? drumNotes[i] : shiftedNote(i);
            playedChannel[i] = drums ? DRUM_MIDI_CHANNEL : MIDI_CHANNEL;

            if (noteViaMidi[i])
            {
                midi.noteOn(playedNote[i], sensors[i].velocity(), playedChannel[i]);
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
                midi.noteOff(playedNote[i], playedChannel[i]);
            }
            else
            {
                speaker.noteOff(playedNote[i]);
            }

            displayCtrl.drawPad(i, false);

            // Zusammen mit den NoteOn-Zeilen zeigt das, ob eine
            // gehaltene Note zwischendurch abreißt (Retrigger)
            Serial.print("NoteOff ");
            Serial.println(playedNote[i]);
        }
    }

    midi.update();

    // OTA: Dienste bei WLAN-Verbindung starten und beide Update-Wege
    // bedienen. Läuft ein Update, blockiert dieser Aufruf bis zum
    // Neustart — Ton und Anzeige übernehmen die OTA-Callbacks.
    if (ENABLE_OTA)
    {
        ota.update();
    }

    // Aftertouch: Druck gehaltener Noten in Modulation übersetzen —
    // getaktet, nicht bei jedem Durchlauf (siehe Config.h)
    if (ENABLE_AFTERTOUCH && millis() - lastAftertouch >= AFTERTOUCH_INTERVAL_MS)
    {
        lastAftertouch = millis();

        updateAftertouch();
    }

    // Übernimmt MIDI die Ausgabe, während der Lautsprecher spielt (Ziel
    // verbindet sich oder MIDI wird im Menü eingeschaltet): Stimmen
    // ausklingen lassen, sonst dudeln sie endlos weiter
    if (ENABLE_SPEAKER)
    {
        bool active = midiActive();

        if (active && !lastMidiConnected)
        {
            speaker.allNotesOff();
        }

        lastMidiConnected = active;
    }

    // Fallende Peak-Marker animieren (intern getaktet)
    displayCtrl.updatePeaks();

    displayCtrl.updateToast();

    // Batterie-Symbol bei niedrigem Ladestand blinken lassen (getaktet)
    displayCtrl.updateBatteryWarning();

    // Encoder: Klick öffnet das Settings-Menü bzw. wechselt den
    // Parameter, Drehen ändert den Wert (geschlossen: Lautstärke-
    // Schnellzugriff). Menü-Timeout und NVS-Speichern in update().
    encoder.update();

    if (encoder.clicked())
    {
        menu.handleClick();

        activity = true;
    }

    int32_t detents = encoder.readDetents();

    if (detents != 0)
    {
        menu.handleRotation(detents);

        activity = true;
    }

    menu.update();

    // Kalibrierung aus dem Menü angefordert (der Board-Button entfällt,
    // er sitzt unzugänglich im Gehäuse)
    if (menu.takeCalibrateRequest())
    {
        recalibrateSensors();
    }

    // Statuszeile höchstens alle 500 ms prüfen
    if (millis() - lastStatusUpdate > 500)
    {
        lastStatusUpdate = millis();

        displayCtrl.showStatus(midi.bleConnected(), midi.wifiConnected(), midi.rtpReady(),
                               midi.setupPortalActive(), ENABLE_SPEAKER && !midiActive());
    }

    // Batterieanzeige in größeren Abständen aktualisieren
    if (millis() - lastBatteryUpdate > BATTERY_UPDATE_MS)
    {
        lastBatteryUpdate = millis();

        displayCtrl.showBattery(readBatteryMilliVolts());
    }

    // Deep Sleep: Timer bei Bedienung zurücksetzen, sonst nach Ablauf
    // einschlafen (kehrt nicht zurück, weckt per Rekalibrier-Button)
    if (ENABLE_DEEP_SLEEP)
    {
        if (activity)
        {
            lastActivity = millis();
        }
        else if (millis() - lastActivity > DEEP_SLEEP_TIMEOUT_MS)
        {
            enterDeepSleep();
        }
    }

    delay(5);
}
