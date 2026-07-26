# Ideen

Was jetzt noch geht, sortiert nach Themen und grob nach Aufwand/Nutzen.
Der aktuelle Stand der Klangerzeugung steht in
[`KLANGERZEUGUNG.md`](KLANGERZEUGUNG.md), die umgesetzten Features
beschreibt die [`README.md`](README.md).

## Klang & Musikalität (der größte Spielraum)

**Vibrato** — der Druck moduliert jetzt die Lautstärke (Aftertouch). Die
Tonhöhe wäre die zweite Richtung: ein LFO, dessen Tiefe am Druck hängt,
oder Pitch-Bend direkt aus dem Druckwert. Beim Speaker ein Multiplikator
auf den Phasenschritt, bei MIDI ein echtes Pitch-Bend-Event.

**Weitere Instrumente** — die Instrument-Weiche (`Instrument`-Enum in
`include/Drums.h`, Umschaltung über `SpeakerController::setInstrument`)
trägt inzwischen drei Klangerzeuger. Neue kommen hinten ans Enum, damit
gespeicherte NVS-Werte gültig bleiben. Naheliegend: ein Bass (Sägezahn
mit Filter-Hüllkurve) oder Pads/Strings.

**Piano/Drums weiter verfeinern** — beide klingen gut, gingen aber noch
besser. Piano: Tine-Attack von der Body-Stimme trennen, mehr
Inharmonizität, echtes (mehrstimmiges) Chorus statt einer verstimmten
Zweitstimme. Drums: Bandpass-Rauschen für Snare/Clap, Layering.

### Weitere Kandidaten

**BLE-MIDI-Empfang** — die umgekehrte Richtung: das BananaPhon als
Klangerzeuger für einen externen Sequencer. Die Infrastruktur dafür ist
mit dem Synth komplett da, es fehlt nur die Empfangsseite im
`MidiController`.

- **Günstig (v1, empfohlen)** — „BLE-Noten klingen über den Speaker":
  `ENABLE_MIDI_RECEIVE` in Config; `MidiController` reicht einen Callback
  an `midiOut.setRawMidiCallback` durch; eine kleine Parse-Funktion
  (NoteOn/NoteOff → Speaker). ~40–60 Zeilen, ~½ Sitzung. Entscheidung
  nötig: Kanal ignorieren (alles auf dem aktuellen Instrument).
- **Moderat** — „richtiger Klangerzeuger": kanalbewusst (Kanal 10 →
  Drumkit, sonst Melodie-Instrument), All-Notes-Off bei Disconnect,
  optional Sustain-CC/Pitch-Bend, „MIDI IN"-Anzeige. ~1 Sitzung.
- **Teuer** — mehr Polyphonie: aktuell 8 Melodie-Stimmen; ein Sequencer
  mit dichten Akkorden stiehlt schnell. Multitimbral über mehrere Kanäle
  gleichzeitig wäre nochmal deutlich mehr.

Drei Haken: (1) die Stimmenzahl ist für Sequencer-Material knapp;
(2) MIDI-Thru-Echo, wenn die DAW „MIDI Thru" an hat; (3) RTP-Empfang
liefe über einen anderen Pfad (AppleMIDI hat eigene Handler) — der
Raw-Callback deckt BLE + USB ab, für RTP wäre separate Verdrahtung nötig.

## Bedienung & Anzeige

**Persistenter Looper-Fortschritt** — der Looper-Zustand ist in der
Icon-Leiste sichtbar (grau/grün/rot); ein Fortschrittsbalken der
Loop-Position (Groovebox-Stil) wäre der nächste Schliff.

**Weitere Menüpunkte** — Touch-Schwellen und Velocity-Kennlinie stecken
noch in der `Config.h`. Sie sind die naheliegendsten Kandidaten fürs
Menü, sobald das Nachjustieren am Gerät (neues Gemüse, andere
Umgebung) lästig wird.

## Code & Infrastruktur

**Unit-Tests für die Logik** — Hüllkurve, Phasen-Akkumulator,
Velocity-Kennlinie, Glitch-Filter, Skalen-Mapping und die Noten-Weiche
sind pure Logik ohne Hardware. Mit PlatformIOs `pio test` (native
environment) ließen die sich auf dem Rechner testen und in die
bestehende CI hängen. (Die DSP-Änderungen werden bereits mit
freistehenden C++-Simulationen geprüft — die ließen sich in echte Tests
gießen.)

**Release-Workflow** — ein GitHub-Actions-Job, der bei einem Tag die
Firmware baut und die `.bin` als Release-Asset anhängt. Zusammen mit ESP
Web Tools (Flashen direkt aus dem Browser) könnten Nachbauer das Gerät
flashen, ohne je PlatformIO zu installieren. Für ein Show-off-Projekt
wie dieses ist das Gold.

**Doku-Feinschliff** — das Foto/GIF bleibt der offene Klassiker; dazu
ein Schaltplan (auch handgezeichnet reicht) mit dem MAX98357A und den
Krokodilklemmen, und optional ein 30-Sekunden-Video. Nichts davon ist
Code, aber nichts würde dem Repo mehr Sterne bringen.
