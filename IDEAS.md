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
- **Splash-Screen** — Name + Firmware-Version beim Start, Kalibrierung
  und Funk-Initialisierung laufen währenddessen im Hintergrund.

## Klang & Musikalität (der größte Spielraum)

**Vibrato/Aftertouch** — die Touch-Werte liefern kontinuierlich Daten,
auch *während* eine Note gehalten wird. Druckänderungen könnten Channel
Pressure (MIDI) bzw. Tonhöhen- oder Lautstärkemodulation (Speaker)
steuern. Das wäre das ausdrucksstärkste Feature überhaupt: die Gurke
fester drücken → der Ton schwillt an.

**Hall/Delay am Speaker** — eine simple Delay-Line würde besonders den
Chip-Wellenformen die Piepser-Anmutung nehmen. Kostet nur RAM für den
Buffer; das Piano zeigt schon, wie viel ein längerer Ausklang bringt.

**Weitere Instrumente** — die Instrument-Weiche (`Instrument`-Enum in
`include/Drums.h`, Umschaltung über `SpeakerController::setInstrument`)
trägt inzwischen drei Klangerzeuger. Neue kommen hinten ans Enum, damit
gespeicherte NVS-Werte gültig bleiben. Naheliegend: ein Bass (Sägezahn
mit Filter-Hüllkurve) oder Pads/Strings.

### Die Bleistift-Idee

Sie hat zwei Ausbaustufen, eine die sofort funktioniert und eine richtig
spannende.

**Stufe 1** (funktioniert heute schon): Bleistiftstriche sind leitfähig
genug für die kapazitive Messung. Man kann sich also jetzt schon eine
Klaviatur aufs Papier zeichnen und die Krokodilklemmen an die
Strichenden klemmen — fertig. Der Strich wirkt als
Elektrodenverlängerung, die Baseline-Kalibrierung frisst den
Graphit-Offset, und sogar die Anschlagsdynamik funktioniert (mehr
Fingerfläche auf dem Strich = höhere Velocity). Wichtig nur: satte,
mehrfach nachgezogene Striche mit weichem Bleistift (B2 oder weicher),
denn dünne HB-Striche haben Megaohm-Widerstände und koppeln schwach. Das
wäre übrigens ein fantastisches README-Foto: gezeichnete Klaviatur statt
Gemüse.

**Stufe 2** (der Ribbon-Controller): Widerstand messen und daraus Töne
modulieren. Physikalisch steckt da ein echtes Instrument drin — ein
langer, dicker Graphitstrich ist ein verteilter Widerstand (grob
50 kΩ–1 MΩ pro cm, je nach Härte und Deckung). Beschaltung im
Makey-Makey-Stil: Strichende über einen ~1-MΩ-Pullup an 3,3 V und an
einen ADC-Pin, der Spieler hält eine GND-Elektrode in der anderen Hand
und berührt den Strich irgendwo — der ADC sieht einen Spannungsteiler
aus Strichsegment + Körperwiderstand. Je weiter vom Clip entfernt
berührt wird, desto höher der Widerstand: ein kontinuierlicher
Positionswert, ein gezeichnetes Ribbon.

Zwei ehrliche Einschränkungen bestimmen das Design. Erstens schwankt der
Hautwiderstand massiv (trockene vs. feuchte Finger: Faktor 10) — der
Absolutwert taugt daher nicht für präzise Tonhöhen, aber hervorragend
für relative Modulation: den Finger auf dem Strich schieben →
Pitch-Bend, Vibrato-Tiefe oder Filter. Genau da glänzt es, ein „Slide"
klingt auf dem Chiptune-Synth herrlich. Alternativ grob quantisiert in
3–5 Zonen (mit Kalibrier-Geste), das ist robust.

Zweitens die Pin-Frage, und die hat einen Haken: Alle Pins sind belegt,
und von den Touch-Pins sind nur GPIO 1, 2, 3 und 10 ADC1-fähig —
GPIO 11–13 hängen an ADC2, der sich mit aktivem WLAN beißt. Der saubere
Weg wäre also: ein Pad opfern (z. B. GPIO 10), `NUM_SENSORS` auf 6, und
dieser Pin wird per Config umschaltbar zum Ribbon-Eingang — sechs
gezeichnete Tasten plus ein gezeichneter Pitch-Bend-Streifen darüber.
Softwareseitig ein kleiner `RibbonController`: ADC oversampled,
Median-Filter, Touch-Erkennung über Schwelle, Wert auf Bend/Vibrato
gemappt — beim Speaker als Multiplikator auf den Phasenschritt, bei MIDI
als echtes Pitch-Bend-Event.

Zu bedenken: `NUM_SENSORS` steckt auch in `scaleIntervals`, `drumNotes`,
`drumLabels` und `drumSpecs` — die Tabellen müssten mitwandern oder auf
eine feste Länge von 7 entkoppelt werden.

### Weitere Kandidaten

**Looper** — Encoder-Longpress startet die Aufnahme der gespielten
Noten, danach loopt sie der Synth und man spielt darüber; das verwandelt
das Gerät vom Instrument zur One-Person-Jam. Mit dem Drumkit als
Grundlage besonders reizvoll: erst einen Beat einspielen, dann die
Melodie darüber.

**BLE-MIDI-Empfang** — die umgekehrte Richtung: das BananaPhon als
Klangerzeuger für einen externen Sequencer. Die Infrastruktur dafür ist
mit dem Synth komplett da, es fehlt nur die Empfangsseite im
`MidiController`.

## Bedienung & Anzeige

**Batterie-Warnung** — die Anzeige ist rein passiv. Unter ~10 % könnte
das Batterie-Symbol blinken oder der Speaker einen dezenten Warnton
spielen, bevor der LiPo in die Tiefentladung läuft.

**Weitere Menüpunkte** — Touch-Schwellen und Velocity-Kennlinie stecken
noch in der `Config.h`. Sie sind die naheliegendsten Kandidaten fürs
Menü, sobald das Nachjustieren am Gerät (neues Gemüse, andere
Umgebung) lästig wird.

## Robustheit & Strom

**Deep Sleep** — im Akkubetrieb läuft das Gerät, bis der LiPo leer ist.
Nach z. B. 10 Minuten ohne Berührung in den Deep Sleep gehen und per
Touch-Wakeup (der ESP32-S3 kann genau das) aufwachen — das verlängert
die Akkulaufzeit von Stunden auf Wochen. Passt gut zusammen mit der
Batterie-Warnung.

**Watchdog + Fehler-Resilienz** — der Audio-Task und die
WiFiManager-Schleife laufen unbeaufsichtigt. Ein Task-Watchdog, der bei
Hängern neu startet, plus ein Boot-Zähler (nach drei Crashs in Folge →
Speaker aus, nur MIDI) wäre die Bühnen-Versicherung.

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
