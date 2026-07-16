Was jetzt noch geht, sortiert nach Themen und grob nach Aufwand/Nutzen:

## Klang & Musikalität (der größte Spielraum)

**Wellenform-Umschalter** — die erwähnte Steilvorlage: Dreieck/Rechteck/Sägezahn/Sinus per Boot-Button durchschalten, aktuelle Wellenform als kleines Symbol im Display. Rechteck und Sägezahn sind im Phasen-Akkumulator je drei Zeilen; Sinus braucht eine kleine Lookup-Tabelle. Wenig Aufwand, viel Charakterunterschied.

**Skalen und Oktav-Shift** — sieben Pads sind fix auf C-Dur gemappt. Ein Skalenwahl-Modus (Dur, Moll, Pentatonik, Blues) plus Oktave hoch/runter macht das Instrument musikalisch vielseitiger, ohne Hardware zu ändern. Die Notentabelle wird dann zur Laufzeit berechnet statt aus der Config gelesen — und die Pad-Beschriftung zieht dank `noteName()` automatisch mit.

**Vibrato/Aftertouch** — die Touch-Werte liefern kontinuierlich Daten, auch *während* eine Note gehalten wird. Druckänderungen könnten Channel Pressure (MIDI) bzw. Tonhöhen- oder Lautstärkemodulation (Speaker) steuern. Das wäre das ausdrucksstärkste Feature überhaupt: die Gurke fester drücken → der Ton schwillt an.

**Sustain/Release-Verlängerung** am Speaker — aktuell klingt eine Note nach 40 ms Release ab. Ein längeres, konfigurierbares Ausklingen (oder ein simpler Hall via Delay-Line) würde den Klang deutlich weniger nach Piepser klingen lassen. Kostet nur RAM für den Delay-Buffer.

## Bedienung & Anzeige

**Einstellungen ohne Neu-Flashen** — Wellenform, Skala, Lautstärke, Velocity-Kennlinie liegen alle in der `Config.h`. Ein kleines Settings-Menü (Boot-Button lang drücken → durchblättern, kurz → ändern) mit Ablage in den NVS-Flash (Preferences-Library) wäre der nächste Reifegrad: Das Gerät wird zum Instrument, das man konfiguriert statt kompiliert. Das ist das größte Einzelprojekt auf dieser Liste.

**Batterie-Warnung** — die Anzeige ist rein passiv. Unter ~10 % könnte das Batterie-Symbol blinken oder der Speaker einen dezenten Warnton spielen, bevor der LiPo in die Tiefentladung läuft.

## Robustheit & Strom

**Deep Sleep** — im Akkubetrieb läuft das Gerät, bis der LiPo leer ist. Nach z. B. 10 Minuten ohne Berührung in den Deep Sleep gehen und per Touch-Wakeup (der ESP32-S3 kann genau das) aufwachen — das verlängert die Akkulaufzeit von Stunden auf Wochen. Passt gut zusammen mit der Batterie-Warnung.

**Watchdog + Fehler-Resilienz** — der Audio-Task und die WiFiManager-Schleife laufen unbeaufsichtigt. Ein Task-Watchdog, der bei Hängern neu startet, plus ein Boot-Zähler (nach drei Crashs in Folge → Speaker aus, nur MIDI) wäre die Bühnen-Versicherung.

## Code & Infrastruktur

**Unit-Tests für die Logik** — Hüllkurve, Phasen-Akkumulator, Velocity-Kennlinie, Glitch-Filter und die Noten-Weiche sind pure Logik ohne Hardware. Mit PlatformIO's `pio test` (native environment) ließen die sich auf dem Rechner testen und in die bestehende CI hängen. Mein Sandbox-Stub war quasi der Vorläufer davon — als echtes `test/`-Verzeichnis im Repo wäre das dauerhaft wertvoll.

**Release-Workflow** — ein GitHub-Actions-Job, der bei einem Tag die Firmware baut und die `.bin` als Release-Asset anhängt. Zusammen mit ESP Web Tools (Flashen direkt aus dem Browser) könnten Nachbauer das Gerät flashen, ohne je PlatformIO zu installieren. Für ein Show-off-Projekt wie dieses ist das Gold.

**Doku-Feinschliff** — das Foto/GIF bleibt der offene Klassiker; dazu ein Schaltplan (auch handgezeichnet reicht) mit dem MAX98357A und den Krokodilklemmen, und optional ein 30-Sekunden-Video. Nichts davon ist Code, aber nichts würde dem Repo mehr Sterne bringen.

Meine Priorisierung, wenn du mich fragst: **Wellenform-Umschalter** (schnell, sofort hörbar) → **Skalen + Oktav-Shift** (macht es zum Instrument) → **Deep Sleep** (macht es alltagstauglich) → **Settings-Menü mit NVS** (bindet alles zusammen). Vibrato ist das spannendste Experiment, aber auch das mit dem meisten Tuning-Aufwand am echten Gemüse. Sag, womit ich anfangen soll — oder ob dich eine Richtung reizt, die hier nicht draufsteht.