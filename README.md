# Gemüse-MIDI-Device

![Build und Check](https://github.com/carsten-walther/Gemuese-MIDI-Device/actions/workflows/ci.yml/badge.svg)

Ein Touch-MIDI-Interface auf Basis des **LilyGo T-Display S3** (ESP32-S3):
Bis zu sieben kapazitive Touch-Sensoren — zum Beispiel Gemüse — lösen
MIDI-Noten aus, die drahtlos per **BLE-MIDI** und **RTP-MIDI (AppleMIDI)**
an einen Computer oder ein iPad gesendet werden. Das Display zeigt die
Pads, den Verbindungsstatus und den Batteriestand.

## Features

- **7 Touch-Eingänge** über die internen Touch-Pins des ESP32-S3,
  mit automatischer Baseline-Kalibrierung beim Start und Hysterese
  gegen Prellen
- **Baseline-Nachführung**: die Ruhewerte folgen langsamer Drift
  (austrocknendes Gemüse, Temperatur) automatisch; der untere
  Board-Button (GPIO 14) kalibriert jederzeit manuell neu — z. B.
  nach dem Umstecken auf neues Gemüse
- **Anschlagsdynamik**: die Velocity wird aus der Touch-Intensität
  abgeleitet (Kontaktfläche) — ein satter Griff klingt lauter als eine
  Fingerspitze; per Peak-Fenster (~10 ms) wird der Spitzenwert des
  Anschlags erfasst
- **BLE-MIDI**: erscheint als Bluetooth-MIDI-Gerät (macOS, iOS, Windows 10+)
- **RTP-MIDI / AppleMIDI** über WLAN inkl. Bonjour/mDNS-Discovery:
  erscheint im Audio-MIDI-Setup (macOS) bzw. in [rtpMIDI](https://www.tobias-erichsen.de/software/rtpmidi.html) (Windows)
- **USB-Host-MIDI** (optional): ein externes MIDI-Gerät kann an den
  ESP32 angeschlossen werden
- **Display-UI**: Pads mit Notennamen — beim Anschlag füllen sie sich
  von unten proportional zur Velocity (grün/gelb/rot, VU-Stil), nach
  dem Loslassen bleibt ein Peak-Hold-Marker auf der letzten Höhe
  stehen —, Statuszeile (BLE / WLAN / RTP), Batterieanzeige mit
  Ladestand bzw. USB-Erkennung

## Hardware

- LilyGo T-Display S3 (Version **ohne** Touchscreen)
- Optional: 1S-LiPo-Akku am JST-Anschluss
- 7 Elektroden (Krokodilklemmen ans Gemüse der Wahl)

### Pinbelegung

| GPIO | Funktion | MIDI-Note |
|------|----------|-----------|
| 1    | Touch-Sensor 1 | C4 (60) |
| 2    | Touch-Sensor 2 | D4 (62) |
| 3    | Touch-Sensor 3 | E4 (64) |
| 10   | Touch-Sensor 4 | F4 (65) |
| 11   | Touch-Sensor 5 | G4 (67) |
| 12   | Touch-Sensor 6 | A4 (69) |
| 13   | Touch-Sensor 7 | H4 (71) |
| 4    | Batteriespannung (intern, 2:1-Teiler) | — |

Mehr interne Touch-Pins gibt es auf diesem Board nicht — GPIO 5–9
gehören dem Display, GPIO 14 dem zweiten Button (hier: Rekalibrierung).
Für mehr Eingänge bietet sich ein externer Touch-Controller
(z. B. MPR121, I2C) an.

## Setup

Voraussetzung: [PlatformIO](https://platformio.org/) (CLI oder VS-Code-Extension).

```sh
git clone https://github.com/carsten-walther/Gemuese-MIDI-Device.git
cd Gemuese-MIDI-Device

# WLAN-Zugangsdaten anlegen (bleiben lokal, sind gitignoriert)
cp include/Credentials.example.h include/Credentials.h
# → include/Credentials.h ausfüllen

# Bauen und flashen
pio run -t upload

# Serieller Monitor (Baseline-Werte, Verbindungsstatus)
pio device monitor
```

**Wichtig beim Start:** In den ersten ~2 Sekunden nach dem Einschalten
werden die Sensoren kalibriert („Kalibriere Touch…" auf dem Display).
Das Gemüse muss dabei schon angeschlossen sein, darf aber nicht
berührt werden.

Im Betrieb führt die Firmware die Ruhewerte automatisch langsam nach
(einstellbar über `TOUCH_BASELINE_INTERVAL_MS` / `TOUCH_BASELINE_FILTER`
in `Config.h`). Nach größeren Änderungen — neues Gemüse, umgesteckte
Klemmen — genügt ein Druck auf den **unteren Board-Button**: alle
Sensoren werden neu kalibriert, dabei ebenfalls nicht berühren.

## MIDI verbinden

**BLE (macOS):** Audio-MIDI-Setup → Fenster → MIDI-Studio →
Bluetooth-Symbol → „Gemuese-MIDI-Device" verbinden.

**RTP-MIDI (macOS):** Audio-MIDI-Setup → MIDI-Studio → Netzwerk-Symbol →
das Gerät erscheint per Bonjour im Verzeichnis → verbinden.

**RTP-MIDI (Windows):** [rtpMIDI](https://www.tobias-erichsen.de/software/rtpmidi.html)
installieren, das Gerät erscheint im Verzeichnis.

## Konfiguration

Alle Einstellungen liegen in [`include/Config.h`](include/Config.h):

- Transports einzeln schaltbar (`ENABLE_BLE_MIDI`, `ENABLE_WIFI_MIDI`,
  `ENABLE_USB_MIDI`)
- Gerätename, MIDI-Kanal, Velocity
- Anschlagsdynamik (`ENABLE_TOUCH_VELOCITY`, Spanne `VELOCITY_MIN` /
  `VELOCITY_MAX`, Kennlinie `TOUCH_VELOCITY_RATIO_MAX`, Peak-Fenster
  `TOUCH_VELOCITY_WINDOW_MS`, Peak-Hold-Marker
  `ENABLE_VELOCITY_PEAK_HOLD`) — der serielle Monitor zeigt die
  gesendete Velocity pro NoteOn zum Einstellen der Kennlinie
- Noten- und Pin-Zuordnung der Sensoren; Notennamen auf dem Display
  wahlweise deutsch (H4) oder englisch (B4) via `USE_GERMAN_NOTE_NAMES`
- Touch-Empfindlichkeit (`TOUCH_ON_RATIO` / `TOUCH_OFF_RATIO`)
- Baseline-Nachführung (`TOUCH_BASELINE_INTERVAL_MS` = 0 schaltet sie ab,
  `TOUCH_BASELINE_FILTER` bestimmt die Trägheit)

**Hinweis zu USB-Host-MIDI:** Der USB-C-Port wird dann exklusiv vom
USB-Host belegt — der serielle Monitor funktioniert nicht mehr, und es
wird ein OTG-Adapter benötigt.

## Projektstruktur

```
include/Config.h            zentrale Konfiguration
include/Credentials.h       WLAN-Zugangsdaten (lokal, gitignoriert)
src/main.cpp                Verdrahtung: Touch → MIDI + Display
src/TouchSensor.*           Touch-Logik (ESP32-S3, Baseline + Hysterese)
src/MidiController.*        MIDI-Transports (BLE, RTP, USB-Host)
src/DisplayController.*     Panel-Konfiguration und UI
scripts/format.py           Format-Target und compiledb-Hook
```

## Entwicklung

```sh
pio run              # Bauen
pio run -t format    # Code formatieren (clang-format, .clang-format)
pio check            # Statische Analyse (cppcheck)
pio run -t compiledb # compile_commands.json für clangd erzeugen
```

Verwendete Bibliotheken:
[ESP32_Host_MIDI](https://github.com/sauloverissimo/ESP32_Host_MIDI),
[LovyanGFX](https://github.com/lovyan03/LovyanGFX),
[AppleMIDI](https://github.com/lathoub/Arduino-AppleMIDI-Library)

## Lizenz

MIT — siehe [LICENSE](LICENSE).
