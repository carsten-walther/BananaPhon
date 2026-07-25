# Ideen

Was jetzt noch geht, sortiert nach Themen und grob nach Aufwand/Nutzen.
Umgesetztes bleibt als kurze Notiz stehen, damit nachvollziehbar ist,
wo das Gerät herkommt.

## Umgesetzt

- **Wellenform-Umschalter** — Dreieck, Rechteck, Sägezahn, Sinus
  (1024er-LUT) und **8-Bit-Chiptune** (25-%-Puls mit 8-Bit-Rasterung in
  der Mix-Stufe, seit v1.0 der Default). Umschaltbar im Settings-Menü.
- **Arpeggio** — gehaltene Akkorde werden im C64-Stil zyklisch als
  schnelle Notenfolge zerlegt (Off/Slow/Fast/Turbo, sample-genau im
  Audio-Task, mit De-Klick-Blende).
- **Skalen und Oktav-Shift** — Dur, Moll, Pentatonik, Blues als
  Menüpunkt (`include/Scales.h`) plus Oktave ±2; die Notentabelle wird
  zur Laufzeit berechnet, die Pad-Beschriftung zieht automatisch mit.
- **Noise-Kanal / Drumkit** — der Vorschlag „ein Pad wird zur
  Snare/HiHat" ist als vollständiger **Drumkit-Modus** gelandet: sieben
  Pads als Kick, Snare, HiHats (zu/offen), zwei Toms und Clap,
  MIDI-seitig als GM-Percussion auf Kanal 10, am Lautsprecher als
  808-Stil-Synthese (Sinus mit Tonhöhen-Hüllkurve auf einen Sockel +
  LFSR-Rauschen mit Tief- bzw. Hochpass, One-Shots mit Velocity).
  Dazu Clap-Mehrfachanschlag, HiHat-Choke und Velocity auf die
  Helligkeit. Rezepte in `include/Drums.h`.
- **Längeres Ausklingen** — mit dem **FM-E-Piano** (2-Operator-FM,
  DX7/Rhodes-Stil) gibt es einen Klang, der bei gehaltener Taste über
  ~2 s weich ausklingt statt nach 40 ms Release abzureißen.
- **Einstellungen ohne Neu-Flashen** — Settings-Menü am Rotary-Encoder
  (Klick = Parameter, Drehen = Wert) für Sound, Wellenform, Arpeggio,
  Skala, Oktave und Lautstärke; Ablage im NVS-Flash (Preferences) mit
  verzögertem, gebündeltem Speichern.
- **Aftertouch** — der Anpressdruck wird bei gehaltener Note laufend
  ausgewertet: Channel Pressure per MIDI, Lautstärke-Modulation am
  Lautsprecher. Bezug ist der eingeschwungene Griff
  (`AFTERTOUCH_SETTLE_MS`), nicht die Anschlagsspitze — sonst fiele
  jede Note direkt nach dem Anschlag ab. Sendetakt und Deadband halten
  den MIDI-Bus frei.
- **Splash-Screen** — Name + Firmware-Version beim Start, Kalibrierung
  und Funk-Initialisierung laufen währenddessen im Hintergrund.
- **Hall/Delay am Speaker** — ein globaler Effekt auf die Mix-Summe,
  im Menü umschaltbar (Off/Delay/Reverb): eine Rückkopplungs-Delay-Line
  (Echo) und ein Freeverb-artiger Hall (8 Kamm- + 4 Allpassfilter).
  Delay und Reverb teilen sich denselben Pufferspeicher (nie
  gleichzeitig aktiv). Auswahl im NVS. Parameter in `Config.h`.
- **OTA-Update** — neue Firmware kabellos über WLAN: ArduinoOTA-Push
  aus PlatformIO oder Upload-Seite im Browser. Fortschritt auf dem
  Display, Fallback auf die alte Partition bei Fehlern.
- **MIDI-Schalter** — die MIDI-Ausgabe lässt sich im Menü abschalten,
  dann spielt das Gerät unabhängig über den Lautsprecher (Standalone),
  auch bei verbundenem MIDI-Ziel.
- **Deep Sleep** — nach 5 Minuten ohne Bedienung geht das Gerät in den
  Tiefschlaf (Display und Funk aus, µA-Bereich) und wacht durch Drehen
  des Encoders wieder auf (ext1 auf der RTC-fähigen Dreh-Spur GPIO18;
  der Encoder-Taster GPIO43 ist nicht RTC-fähig und kann Deep Sleep
  nicht wecken). Beim Einschlafen wird der Ruhepegel gelesen und auf den
  Gegenpegel geweckt — so weckt jede Drehung unabhängig von der
  Rasterstellung. Die manuelle Rekalibrierung wanderte dabei vom
  (unzugänglichen) Board-Button in den Menüpunkt **Calibrate**.
- **Batterie-Warnung** — unter `BATTERY_WARN_PERCENT` (Default 5 %)
  blinkt das Batterie-Symbol im Akkubetrieb, bevor der LiPo in die
  Tiefentladung läuft.
- **Werkseinstellungen** — Menüpunkt **Factory Reset**: setzt nach einer
  zweistufigen Sicherheits-Rückfrage (zweimal drehen) alle NVS-Werte
  zurück und startet neu.

## Klang & Musikalität (der größte Spielraum)

**Vibrato** — der Druck moduliert jetzt die Lautstärke (siehe
Aftertouch oben). Die Tonhöhe wäre die zweite Richtung: ein LFO, dessen
Tiefe am Druck hängt, oder Pitch-Bend direkt aus dem Druckwert. Beim
Speaker ein Multiplikator auf den Phasenschritt, bei MIDI ein echtes
Pitch-Bend-Event.

**Weitere Instrumente** — die Instrument-Weiche (`Instrument`-Enum in
`include/Drums.h`, Umschaltung über `SpeakerController::setInstrument`)
trägt inzwischen drei Klangerzeuger. Neue kommen hinten ans Enum, damit
gespeicherte NVS-Werte gültig bleiben. Naheliegend: ein Bass (Sägezahn
mit Filter-Hüllkurve) oder Pads/Strings.

### Weitere Kandidaten

**Looper** — Encoder-Longpress startet die Aufnahme der gespielten
Noten, danach loopt sie der Synth und man spielt darüber; das verwandelt
das Gerät vom Instrument zur One-Person-Jam. Mit dem Drumkit als
Grundlage besonders reizvoll: erst einen Beat einspielen, dann die
Melodie darüber. Hierzu muss die Ausgabe am Display angepasst werden, 
eher Richtung Drumm-Computer.

**BLE-MIDI-Empfang** — die umgekehrte Richtung: das BananaPhon als
Klangerzeuger für einen externen Sequencer. Die Infrastruktur dafür ist
mit dem Synth komplett da, es fehlt nur die Empfangsseite im
`MidiController`.

## Bedienung & Anzeige

**Weitere Menüpunkte** — Touch-Schwellen und Velocity-Kennlinie stecken
noch in der `Config.h`. Sie sind die naheliegendsten Kandidaten fürs
Menü, sobald das Nachjustieren am Gerät (neues Gemüse, andere
Umgebung) lästig wird.

## Robustheit & Strom

**Watchdog + Fehler-Resilienz** — der Audio-Task und die
WiFiManager-Schleife laufen unbeaufsichtigt. Ein Task-Watchdog, der bei
Hängern neu startet, plus ein Boot-Zähler (nach drei Crashs in Folge →
Speaker aus, nur MIDI) wäre die Bühnen-Versicherung. Den Zähler braucht es
eigentlich nicht.

## Code & Infrastruktur

**Unit-Tests für die Logik** — Hüllkurve, Phasen-Akkumulator,
Velocity-Kennlinie, Glitch-Filter, Skalen-Mapping und die Noten-Weiche
sind pure Logik ohne Hardware. Mit PlatformIOs `pio test` (native
environment) ließen die sich auf dem Rechner testen und in die
bestehende CI hängen.

**Release-Workflow** — ein GitHub-Actions-Job, der bei einem Tag die
Firmware baut und die `.bin` als Release-Asset anhängt. Zusammen mit ESP
Web Tools (Flashen direkt aus dem Browser) könnten Nachbauer das Gerät
flashen, ohne je PlatformIO zu installieren. Für ein Show-off-Projekt
wie dieses ist das Gold.

**Doku-Feinschliff** — das Foto/GIF bleibt der offene Klassiker; dazu
ein Schaltplan (auch handgezeichnet reicht) mit dem MAX98357A und den
Krokodilklemmen, und optional ein 30-Sekunden-Video. Nichts davon ist
Code, aber nichts würde dem Repo mehr Sterne bringen.
