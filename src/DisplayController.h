#pragma once

#include <Arduino.h>

#include "Config.h"

class DisplayController
{
public:
    // Schaltet die LCD-Versorgung ein, initialisiert das Panel
    // und zeichnet den Titel.
    void begin();

    // Splash-Screen (Name + Version + Kalibrier-Hinweis); danach
    // showHome() aufrufen, um die normale Oberfläche aufzubauen
    void showSplash();

    // Baut nach dem Splash die normale Oberfläche neu auf
    void showHome();

    // Hinweis während der Touch-Kalibrierung
    void showCalibrating();

    // Zeichnet alle Pads im Ruhezustand (nach der Kalibrierung)
    void showPads();

    // Zeichnet ein einzelnes Pad. Gedrückt: Füllstand von unten
    // proportional zur Velocity (VU-Stil, grün/gelb/rot);
    // losgelassen: Ruhezustand.
    void drawPad(uint8_t index, bool pressed, uint8_t velocity = 0);

    // Lässt gehaltene Peak-Marker nach Ablauf der Haltezeit animiert
    // nach unten fallen — regelmäßig aus loop() aufrufen (intern auf
    // VELOCITY_PEAK_FALL_INTERVAL_MS getaktet, sonst ein No-Op).
    void updatePeaks();

    // Kurzzeitige Einblendung im Freiraum zwischen Statusleiste und
    // Tasten; verschwindet nach `durationMs` von selbst
    void showToast(const char* text, uint32_t durationMs = DISPLAY_TOAST_MS);

    // Oktav-Shift und Skala für die Tastenbeschriftung (Anzeige folgt
    // den tatsächlich gespielten Noten); danach showPads() aufrufen
    void setOctave(int8_t octave);
    void setScale(uint8_t scale);

    // Instrument: Drums zeigen Kürzel (KD, SN, …) statt Notennamen
    void setInstrument(uint8_t instrument);

    // Räumt eine abgelaufene Einblendung weg — aus loop() aufrufen
    void updateToast();

    // Aktualisiert die Statuszeile; zeichnet nur bei Änderung neu.
    // `portal` = WLAN-Setup-Portal aktiv, `speaker` = Standalone-
    // Betrieb über den Lautsprecher (kein MIDI-Ziel verbunden).
    // `looper`: 0 = aus/gestoppt, 1 = Wiedergabe, 2 = Aufnahme
    void showStatus(bool ble, bool wifi, bool rtp, bool portal = false, bool speaker = false,
                    uint8_t looper = 0);

    // Batterieanzeige oben rechts; zeichnet nur bei Änderung neu.
    // milliVolts: Batteriespannung (bereits mit Spannungsteiler
    // verrechnet). Werte über ~4,4 V bedeuten USB-Versorgung.
    void showBattery(uint32_t milliVolts);

    // Lässt das Batterie-Symbol unter BATTERY_WARN_PERCENT blinken
    // (nur im Akkubetrieb) — regelmäßig aus loop() aufrufen, intern
    // auf BATTERY_WARN_BLINK_MS getaktet.
    void updateBatteryWarning();

    // Vollbild-Anzeige während eines OTA-Firmware-Updates. percent < 0
    // baut den Screen neu auf (Titel + Statuszeile, kein Balken);
    // percent >= 0 aktualisiert nur den Fortschrittsbalken. `color`
    // färbt die Statuszeile (z. B. Rot bei Fehler, Grün bei Erfolg).
    void showOtaScreen(const char* status, int percent, uint16_t color = 0xFFFF);

    // Abschieds-Screen vor dem Deep Sleep (kurz stehen lassen, dann
    // powerOff())
    void showSleep();

    // Panel und Hintergrundbeleuchtung abschalten und die LCD-
    // Versorgung kappen — vor dem Deep Sleep, damit nichts leuchtet
    void powerOff();

private:
    bool _lastBle       = false;
    bool _lastWifi      = false;
    bool _lastRtp       = false;
    bool _lastPortal    = false;
    bool _lastSpeaker   = false;
    uint8_t _lastLooper = 0;

    bool _statusDrawn = false;

    int _lastBatPercent = -1;
    bool _lastUsbPower  = false;

    // Zeichnet das Batterie-Symbol aus dem letzten Messwert
    // (visible=false: nur Bereich löschen, Blink-Aus-Phase)
    void renderBattery(bool visible);

    // Warnblinken unter niedrigem Ladestand
    bool _batBlinkOn       = true;
    uint32_t _lastBatBlink = 0;

    // Peak-Hold je Pad: Markerhöhe in px ab Pad-Unterkante (0 = kein
    // Marker), Velocity für die Markerfarbe, Ende der Haltezeit, und
    // ob das Pad gerade gedrückt ist (dann kein Marker/Fallen).
    int32_t _peakPos[NUM_SENSORS]        = {0};
    uint8_t _peakVelocity[NUM_SENSORS]   = {0};
    uint32_t _peakHoldUntil[NUM_SENSORS] = {0};
    bool _padPressed[NUM_SENSORS]        = {false};

    uint32_t _lastPeakStep = 0;

    uint32_t _toastUntil = 0;

    int8_t _octave = 0;

    uint8_t _scale = 0;

    uint8_t _instrument = 0;
};
