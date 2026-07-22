#pragma once

#include <Arduino.h>

// ------------------------------------------------
// MIDI-Transports
// ------------------------------------------------

// BLE-MIDI: erscheint als Bluetooth-MIDI-Gerät (macOS, iOS, Windows 10+)
constexpr bool ENABLE_BLE_MIDI = true;

// RTP-MIDI (AppleMIDI) über WLAN: erscheint in Audio-MIDI-Setup (macOS)
// bzw. rtpMIDI (Windows). Braucht gültige WLAN-Zugangsdaten (unten).
constexpr bool ENABLE_WIFI_MIDI = true;

// USB-Host-MIDI: ein MIDI-Gerät wird AN den ESP32 angeschlossen (OTG).
// Achtung: belegt den USB-Port exklusiv, der Serial-Monitor über USB
// funktioniert dann nicht mehr.
constexpr bool ENABLE_USB_MIDI = false;

// Gerätename (BLE-Advertising, RTP-MIDI/mDNS, Setup-Portal-AP und
// Splash-Screen)
constexpr char MIDI_DEVICE_NAME[] = "BananaPhon";

// Firmware-Version (Splash-Screen); bei Releases mitziehen
constexpr char FIRMWARE_VERSION[] = "1.0.0";

// WLAN-Setup-Portal: kommt WIFI_PORTAL_AFTER_MS nach dem Start keine
// Verbindung zustande (oder ist WIFI_SSID in Credentials.h leer),
// spannt das Gerät einen Access Point mit Captive Portal auf. Dort
// eingetragene Zugangsdaten landen im Flash und überleben Neustarts —
// kein Neu-Flashen für einen WLAN-Wechsel nötig. BLE-MIDI und die
// Pads laufen währenddessen normal weiter.
constexpr bool ENABLE_WIFI_PORTAL = true;

constexpr uint32_t WIFI_PORTAL_AFTER_MS = 30000;

// ------------------------------------------------
// WLAN (nur für RTP-MIDI nötig)
// ------------------------------------------------
//
// Die Zugangsdaten liegen in include/Credentials.h (nicht im Git).
// Vorlage: include/Credentials.example.h

#include "Credentials.h"

// ------------------------------------------------
// MIDI
// ------------------------------------------------

constexpr uint8_t MIDI_CHANNEL     = 1;
constexpr uint8_t DEFAULT_VELOCITY = 100;

// Anschlagsdynamik: Velocity aus der Touch-Intensität ableiten.
// Der kapazitive Messwert steigt mit der Kontaktfläche — ein satter
// Griff ergibt höhere Werte als eine Fingerspitze. Bei false wird
// immer DEFAULT_VELOCITY gesendet.
constexpr bool ENABLE_TOUCH_VELOCITY = true;

// Velocity-Spanne und Kennlinie: VELOCITY_MIN wird bei einem Messwert
// knapp über der ON-Schwelle gesendet, VELOCITY_MAX ab
// baseline * TOUCH_VELOCITY_RATIO_MAX (dazwischen linear).
// Liegt VELOCITY_RATIO_MAX zu hoch, erreicht man das Maximum nie —
// die eigenen Peak-Werte zeigt der serielle Monitor.
constexpr uint8_t VELOCITY_MIN = 32;
constexpr uint8_t VELOCITY_MAX = 127;

// 1.60 war zu eng: am Gerät sättigten satte Anschläge bei Velocity 127
// und der Aftertouch-Druck lief beim Nachdrücken gegen die 127-Decke
// (real gemessen). 2.00 spannt die Kennlinie weiter auf — Anschläge
// differenzieren oben besser, und Nachdrücken hat Spielraum.
constexpr float TOUCH_VELOCITY_RATIO_MAX = 2.00f;

// Peak-Hold auf dem Display: nach dem Loslassen bleibt eine dünne
// Markerlinie auf der Höhe der letzten Velocity stehen, hält dort
// VELOCITY_PEAK_HOLD_MS und fällt dann animiert nach unten
// (alle FALL_INTERVAL_MS um FALL_PX Pixel), bis sie verschwindet.
constexpr bool ENABLE_VELOCITY_PEAK_HOLD = true;

constexpr uint32_t VELOCITY_PEAK_HOLD_MS          = 250;
constexpr uint32_t VELOCITY_PEAK_FALL_INTERVAL_MS = 25;
constexpr int32_t VELOCITY_PEAK_FALL_PX           = 2;

// Peak-Fenster in ms: nach dem Überschreiten der ON-Schwelle wird so
// lange der Spitzenwert gesammelt, erst dann geht das NoteOn raus —
// der Messwert steigt beim Auftreffen des Fingers noch einige ms an.
// 0 = kein Fenster (NoteOn sofort, Velocity aus der ersten Messung).
constexpr uint32_t TOUCH_VELOCITY_WINDOW_MS = 10;

// ------------------------------------------------
// Aftertouch (Druckdynamik)
// ------------------------------------------------
//
// Der kapazitive Messwert ändert sich auch WÄHREND eine Note gehalten
// wird — fester greifen heißt mehr Kontaktfläche heißt höherer Wert.
// Daraus wird Channel Pressure (MIDI) bzw. am Lautsprecher eine
// Lautstärke-Modulation: fester drücken lässt den Ton anschwellen.
constexpr bool ENABLE_AFTERTOUCH = true;

// Glättung des Druckwerts (IIR-Tiefpass, größer = träger). Der
// Rohwert zittert deutlich; ungefiltert zittert der Ton mit.
constexpr uint8_t AFTERTOUCH_FILTER = 8;

// Sendetakt und Mindeständerung. Ohne beides ginge pro gehaltener
// Note bei jedem Loop-Durchlauf eine Nachricht raus (~200/s) — das
// verstopft besonders BLE-MIDI.
constexpr uint32_t AFTERTOUCH_INTERVAL_MS = 25;
constexpr uint8_t AFTERTOUCH_DEADBAND     = 3;

// Einschwingzeit bis zum Bezugspunkt: Der Messwert fällt nach dem
// Anschlag noch deutlich ab (die Anschlagsspitze liegt über dem
// gehaltenen Griff). Erst nach dieser Zeit wird der Ruhedruck als
// Bezug festgehalten — sonst startete jede Note im Minus und würde
// gleich nach dem Anschlag leiser. Bis dahin moduliert nichts.
//
// Der Wert muss deutlich über der Zeitkonstante der Glättung liegen
// (AFTERTOUCH_FILTER * Loop-Intervall, also ~40 ms): bei 60 ms wäre
// der Bezug erst zu 78 % eingeschwungen und der Ton bliebe dauerhaft
// ~5 % zu leise. Bei 200 ms trifft er den Ruhedruck. Kürzere Noten
// bekommen dadurch gar keinen Aftertouch — was sie ohnehin nicht
// sinnvoll nutzen könnten.
constexpr uint32_t AFTERTOUCH_SETTLE_MS = 200;

// Modulationsbereich am Lautsprecher. Der Faktor wirkt auf die
// Lautstärke der Stimme und ist relativ zum Druck beim Anschlag:
// beim Anschlag immer 1.0, danach hoch bis MAX oder runter bis MIN.
// So bleibt die Anschlagsdynamik erhalten und der Druck moduliert nur.
constexpr float AFTERTOUCH_SPEAKER_MIN = 0.35f;
constexpr float AFTERTOUCH_SPEAKER_MAX = 1.80f;

// Hinweis: Die Kennlinie ist dieselbe wie bei der Velocity und
// sättigt bei TOUCH_VELOCITY_RATIO_MAX. Wer schon beim Anschlag voll
// zugreift, hat nach oben keinen Spielraum mehr — der serielle
// Monitor zeigt die Druckwerte zum Einstellen.

// Anzahl der Sensoren
constexpr uint8_t NUM_SENSORS = 7;

// Notennamen auf dem Display: deutsch (H statt B, wie in der README)
// oder englisch (B). Betrifft nur die Anzeige, nie die MIDI-Noten.
constexpr bool USE_GERMAN_NOTE_NAMES = true;

// Grundton der Skalen (C4); die Zuordnung der Pads zu Noten ergibt
// sich aus der im Menü gewählten Skala (include/Scales.h) plus
// Oktav-Shift
constexpr uint8_t SCALE_ROOT_NOTE = 60;

// Touch-Pins
//
// GPIO 1, 2, 3, 10, 11, 12, 13 sind auf den Stiftleisten des T-Display S3
// herausgeführt und touch-fähig (T1–T13), ohne Konflikt mit dem Display.
// Weitere interne Touch-Pins gibt es auf diesem Board nicht (GPIO 4–9
// und 14 sind durch Batterie-Messung, Display und Button belegt).
//
constexpr uint8_t touchPins[NUM_SENSORS] = {1, 2, 3, 10, 11, 12, 13};

// ------------------------------------------------
// Batterie
// ------------------------------------------------

// Batteriespannung, über 2:1-Spannungsteiler auf dem Board (ADC1,
// daher kein Konflikt mit aktivem WLAN)
constexpr uint8_t PIN_BAT_VOLT = 4;

// Messintervall in Millisekunden
constexpr uint32_t BATTERY_UPDATE_MS = 5000;

// ------------------------------------------------
// Touch-Kalibrierung (ESP32-S3)
// ------------------------------------------------
//
// Beim ESP32-S3 STEIGT der touchRead()-Wert bei Berührung (anders als
// beim klassischen ESP32). Beim Start wird pro Sensor eine Baseline
// gemessen; "gedrückt" gilt, wenn der Wert die Baseline um den
// jeweiligen Faktor übersteigt (Hysterese: ON-Faktor > OFF-Faktor).

constexpr float TOUCH_ON_RATIO  = 1.15f;
constexpr float TOUCH_OFF_RATIO = 1.08f;

constexpr uint8_t TOUCH_CALIBRATION_SAMPLES = 16;

// Glitch-Filter: so viele aufeinanderfolgende Messungen müssen über der
// ON-Schwelle liegen, bevor ein Anschlag beginnt. Ein einzelner
// Messwert-Ausreißer (ADC-Glitch, WLAN-Burst) löst so keine Geisternote
// mehr aus; bei 2 Messungen kostet das nur einen Loop-Durchlauf (~5 ms).
// 1 = Filter aus (jede Messung zählt sofort).
constexpr uint8_t TOUCH_CONFIRM_SAMPLES = 2;

// Baseline-Nachführung: solange ein Sensor NICHT gedrückt ist, folgt
// seine Baseline dem Messwert langsam (IIR-Tiefpass). Das kompensiert
// Drift durch austrocknendes Gemüse, Temperatur oder Kabellage.
//
// Alle TOUCH_BASELINE_INTERVAL_MS wird die Baseline um 1/FILTER der
// aktuellen Abweichung nachgezogen — mit 250 ms und Faktor 16 ergibt
// das eine Zeitkonstante von ~4 s: langsame Drift wird ausgeglichen,
// eine normale Berührung (deutlich kürzer) verschiebt fast nichts.
constexpr uint32_t TOUCH_BASELINE_INTERVAL_MS = 250; // 0 = Nachführung aus
constexpr uint16_t TOUCH_BASELINE_FILTER      = 16;

// ------------------------------------------------
// Lautsprecher (Standalone-Synth)
// ------------------------------------------------

// Ist kein MIDI-Ziel verbunden (weder BLE noch RTP), spielt ein
// I2S-Verstärker (MAX98357A) die Noten direkt: polyphon, eine
// Dreieck-Stimme pro Pad, Velocity = Lautstärke der Stimme.
constexpr bool ENABLE_SPEAKER = true;

// I2S-Pins zum MAX98357A (BCLK/LRC/DIN); 5V und GND nicht vergessen
constexpr uint8_t PIN_I2S_BCLK  = 21;
constexpr uint8_t PIN_I2S_LRCLK = 17;
constexpr uint8_t PIN_I2S_DOUT  = 16;

// Abtastrate des Synths. Alle Hüllkurvenzeiten (Drums.h, Attack/
// Release unten) sind in Millisekunden angegeben und werden zur
// Laufzeit umgerechnet — die Rate ist damit frei wählbar, ohne den
// Klangcharakter zu verschieben. 32 kHz hebt das Rauschdach von 11 auf
// 16 kHz: HiHats und Clap zischen, statt gedeckelt zu rauschen. Kostet
// ~45 % mehr Rechenzeit im Audio-Task (unkritisch, eigener Core).
constexpr uint32_t SPEAKER_SAMPLE_RATE = 32000;

// Gesamtlautstärke 0.0–1.0 (Hardware-Feintrim über den Gain-Pin
// des MAX98357A)
constexpr float SPEAKER_MASTER_VOLUME = 0.6f;

// Arpeggio: gehaltene Noten werden zyklisch als schnelle Folge
// gespielt statt gleichzeitig (der klassische C64-Trick). Stufen:
// Off / Slow / Fast / Turbo — Schrittdauer in ms (0 = aus).
constexpr uint32_t ARP_STEP_MS[] = {0, 120, 60, 30};

constexpr uint8_t ARP_MODE_COUNT = 4;

// Hüllkurve pro Stimme: kurze Rampen verhindern Knackser bei
// NoteOn/NoteOff
constexpr uint32_t SPEAKER_ATTACK_MS  = 5;
constexpr uint32_t SPEAKER_RELEASE_MS = 40;

// ------------------------------------------------
// Buttons
// ------------------------------------------------

// Unterer Board-Button (aktiv LOW): manuelle Rekalibrierung aller
// Sensoren im Betrieb — z. B. nach dem Umstecken auf neues Gemüse.
// Während der Kalibrierung die Elektroden nicht berühren!
constexpr uint8_t PIN_BUTTON_RECALIBRATE = 14;

// ------------------------------------------------
// Display
// ------------------------------------------------

// Display Rotation 0 ... 3 (sinnvoll sind 1 und 3)
// Je nach dem, auf welcher Seite der Rotary-Encode platziert ist.
// Die USB-Schnittstelle soll frei beleiben, also rechts von Display.
constexpr uint8_t DISPLAY_ROTATION = 3;

// Display Helligkeit 0 ... 255
constexpr uint8_t DISPLAY_BRIGHNESS = 255;

// Anzeigedauer für kurzzeitige Einblendungen (z. B. Lautstärke)
constexpr uint32_t DISPLAY_TOAST_MS = 1500;

// Mindestdauer des Splash-Screens beim Start; die Touch-Kalibrierung
// und die Funk-Initialisierung laufen währenddessen im Hintergrund
constexpr uint32_t SPLASH_MS = 2000;

// ------------------------------------------------
// Settings-Menü
// ------------------------------------------------

// Encoder-Klick öffnet das Menü: Drehen ändert den Wert, Klicken
// wechselt zum nächsten Parameter (Lautstärke, Wellenform, Oktave).
// Ohne Eingabe schließt es sich nach dem Timeout; geänderte Werte
// landen im NVS-Flash und überleben Neustarts.
constexpr uint32_t MENU_TIMEOUT_MS = 4000;

// Oktav-Shift-Bereich (± in Oktaven)
constexpr int8_t OCTAVE_RANGE = 2;

// ------------------------------------------------
// Rotary-Encoder
// ------------------------------------------------

// EC11/KY-040 an der Stiftleiste: A/B als Quadratursignal (Auswertung
// per PCNT-Hardware), SW als Taster gegen GND (Pull-up intern).
// Hinweis: 43/44 sind U0TXD/U0RXD — frei nutzbar, weil der serielle
// Monitor auf dem S3 über natives USB-CDC läuft, nicht über UART0.
constexpr bool ENABLE_ENCODER = true;

constexpr uint8_t PIN_ENCODER_A  = 44;
constexpr uint8_t PIN_ENCODER_B  = 18;
constexpr uint8_t PIN_ENCODER_SW = 43;

// Zählimpulse pro Raste (EC11: 4 bei voller Quadratur-Auswertung)
constexpr int32_t ENCODER_STEPS_PER_DETENT = 4;

// Lautstärkeänderung pro Raste im Standalone-Betrieb
constexpr float ENCODER_VOLUME_STEP = 0.05f;
