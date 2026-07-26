# Klangerzeugung

Wie das BananaPhon seine Klänge im Standalone-Betrieb selbst erzeugt
(der Lautsprecher-Synth im `SpeakerController`). Im MIDI-Betrieb gehen
statt Audio nur Noten raus — dann klingt, was am anderen Ende hängt.

Dieses Dokument beschreibt den **aktuellen Stand** und wird bei
Änderungen an der Synthese mitgezogen. Die Zahlenwerte stehen in
[`include/Config.h`](include/Config.h) (Engine) und
[`include/Drums.h`](include/Drums.h) (Instrument-Parameter); der Code
liegt in [`src/SpeakerController.cpp`](src/SpeakerController.cpp).

## Gemeinsame Grundlagen

- **Abtastrate** `SPEAKER_SAMPLE_RATE` = 32 kHz. Der Audio-Task rendert
  auf **Core 0** in Blöcken von `FRAMES` = 128 Samples (~4 ms Latenz)
  und schiebt sie per I2S an den MAX98357A-Verstärker. Touch, MIDI,
  Display und Funk laufen davon unbeeinflusst auf Core 1.
- **Phasen-Akkumulator**: Jede Stimme hat eine 32-Bit-Phase, die pro
  Sample um einen aus der Frequenz berechneten Schritt weiterzählt
  (`stepForFreq` / `stepForNote`, `f = 440·2^((note−69)/12)`). Die
  oberen 10 Bit adressieren eine **Sinustabelle** mit 1024 Einträgen —
  `sinf()` pro Sample wäre zu teuer.
- **Hüllkurven ratenunabhängig**: Alle Zeiten stehen in Millisekunden
  und werden zur Laufzeit in Pro-Sample-Faktoren umgerechnet
  (`decayPerSample(ms)` = Abfall auf −60 dB nach ms; `onePoleCoeff(hz)`
  für Filter). Die Abtastrate ist damit frei wählbar, ohne den Klang zu
  verschieben.
- **Stimmen-Pools**: `NUM_VOICES` = 15 Stimmen, aufgeteilt in
  **7 Drum-Stimmen** (fest nach Pad-Index 0..6) und **8 Melodie-Stimmen**
  dahinter (`MELODY_VOICES`). Die Trennung ist wichtig für den Looper:
  So kann ein Drum-Loop parallel zu Live-Melodie klingen, ohne dass eine
  Drum eine Melodie-Stimme mit gleichem Index überschreibt (das knackte).
- **Mix-Stufe**: Alle Stimmen werden summiert, jede mit ihrer eigenen
  Kopffreiheit (`DRUM_HEADROOM` 2.5, `MELODY_HEADROOM` = 7·1.5 = 10.5) —
  Drums dürfen laut, die stapelbaren Melodie-Stimmen leiser. Danach
  `masterVolume` (`SPEAKER_MASTER_VOLUME` 0.6), eine **weiche Sättigung**
  (kubisch `1.5x − 0.5x³`, Spitzen gerundet statt hart abgeschnitten) und
  optional ein Effekt (siehe unten).

## Chip-Synth (`INST_CHIP`)

Der klassische 8-Bit-Synthesizer, eine Stimme pro gehaltenem Pad.

- **Wellenform** wählbar (`setWaveform`): Dreieck, Rechteck (±28000, etwas
  leiser weil subjektiv lauter), Sägezahn, Sinus (LUT) und **8-Bit-Chip**
  (25-%-Pulswelle ±26000, hohl-nasal wie ein NES).
- **Hüllkurve**: kurze lineare Rampen `SPEAKER_ATTACK_MS` (5 ms) und
  `SPEAKER_RELEASE_MS` (40 ms) gegen Knackser; Velocity = Zielamplitude.
- **Bitcrusher**: Nur im Chip-Instrument mit Chip-Wellenform wird das
  Ausgangssample auf 8 Bit gerastert (`s &= ~0xFF`). Das erzeugt das
  typische „Zipper"-Treppchen der 80er-Soundchips auf Hüllkurve und
  Stimmensumme. Drums/Piano bleiben davon verschont.

## FM-E-Piano (`INST_PIANO`)

Zwei-Operator-FM im DX7/Rhodes-Stil: Ein **Modulator** verschiebt die
Ablese-Phase des **Trägers** in der Sinustabelle (Phasenmodulation).

- **Modulationsverhältnis** `PIANO_MOD_RATIO` = 5 (Modulator = 5-fache
  Tonhöhe) → heller, glockiger Klang.
- **Anschlags-Glanz**: Der Modulationsindex startet hoch
  (`PIANO_INDEX_START` 350) und fällt mit `PIANO_INDEX_DECAY_MS` (75 ms)
  auf einen Sockel (`PIANO_INDEX_FLOOR` 45) — der metallische Anschlag
  wird schnell weich (die „Tine").
- **Velocity → Helligkeit**: Die Anschlagstärke skaliert den
  Start-Index (`PIANO_VEL_INDEX_MIN` 0.35 = Anteil bei Velocity 0), nicht
  nur die Lautstärke — ein satter Hammerschlag klingt obertonreicher.
- **Amplitude**: perkussiver Einsatz (sofort auf Zielamplitude, Sinus
  startet bei Phase 0 → kein Klick), dann langes Ausklingen
  `PIANO_DECAY_MS` (2000 ms) bei gehaltener Taste, schnelleres Release
  `PIANO_RELEASE_MS` (150 ms) nach dem Loslassen.
- **Wärme gegen den synthetischen Klang** (reines FM klingt perfekt-
  harmonisch und statisch):
  - `PIANO_MOD_DETUNE` (0.02) macht den Modulator leicht **inharmonisch**
    — die Obertöne stehen nicht perfekt harmonisch, wie leicht gestreckte
    Klaviersaiten.
  - Ein **zweiter, minimal verstimmter Träger** (`PIANO_DETUNE` 0.006 ≈
    10 ct, beigemischt mit `PIANO_CHORUS_MIX` 0.15) erzeugt Schwebung und
    Breite (Rhodes-artiger Chorus). Größerer Mix = mehr Schwebung, aber
    auch mehr Lautstärke-Wobble.

## Drumkit (`INST_DRUMS`)

Sieben One-Shot-Stimmen im 808-Stil, eine feste pro Pad. Jede Drum ist
**Sinus-Ton + weißes Rauschen**, gemischt und gefiltert. Rezepte in
`drumSpecs[]` (`include/Drums.h`).

- **Ton-Anteil**: Sinus mit `freq`, dessen Tonhöhe exponentiell in
  `sweepMs` auf `pitchFloor` fällt (der „Punch"). Der Sockel verhindert,
  dass die Tonhöhe gegen 0 Hz wegläuft und die Drum verstummt.
- **Rausch-Anteil**: **weißes Rauschen per xorshift32** (mehrstufig,
  weich — das frühere 1-Bit-Rauschen klang mit Crest-Faktor 1.0 blechern).
  Gefiltert entweder als **1-poliger Tiefpass** (Snare, Toms) oder als
  **2-poliger Hochpass** (HiHats, Clap; 12 dB/Okt nimmt den blechernen
  Mittenhonk, lässt luftiges Zischen).
- **Hüllkurve**: fester exponentieller Abfall `decayMs` (One-Shot,
  NoteOff wird ignoriert). Velocity = Anschlagstärke; bei den
  Tiefpass-Drums öffnet sie zusätzlich die Rausch-Eckfrequenz
  (`DRUM_VEL_TONE_MIN` 0.6 = Anteil bei Velocity 0) → härter = heller.
- **Clap**: kein Einzelschlag, sondern `bursts` dicht gestaffelte
  Anschläge im `burstMs`-Raster (10 ms), danach der Tail.
- **Choke**: Ein Schlag kann eine andere Drum abwürgen (`chokes`) — die
  geschlossene HiHat beendet die offene, wie auf einem echten Kit. Statt
  hartem Abschalten ein kurzer Fade (`DRUM_CHOKE_MS` 5 ms).
- **Retrigger**: Wird dieselbe Drum angeschlagen, während sie noch
  klingt, blendet die Stimme erst per Choke-Fade aus und wird danach neu
  angeschlagen — ein harter Reset mitten in der Schwingung würde knacken.

Aktuelle Rezepte (GM-Note auf MIDI-Kanal 10 in Klammern):

| Pad | Drum | GM | freq→floor / sweep | decay | Ton/Rausch | Rauschfilter |
|-----|------|----|--------------------|-------|------------|--------------|
| KD | Kick | 36 | 170→50 Hz / 50 ms | 320 ms | 1.00 / 0.00 | — |
| SN | Snare | 38 | 190→180 Hz / 4 ms | 140 ms | 0.45 / 0.90 | TP 1250 Hz |
| HH | HiHat zu | 42 | nur Rauschen | 70 ms | 0.00 / 1.00 | HP 3200 Hz, würgt OH |
| OH | HiHat offen | 46 | nur Rauschen | 350 ms | 0.00 / 0.80 | HP 2800 Hz |
| T1 | Tom tief | 45 | 105→80 Hz / 60 ms | 250 ms | 1.00 / 0.06 | TP 1000 Hz |
| T2 | Tom hoch | 50 | 160→120 Hz / 60 ms | 250 ms | 1.00 / 0.06 | TP 1000 Hz |
| CP | Clap | 39 | nur Rauschen | 160 ms | 0.00 / 0.95 | HP 1250 Hz, 3 Bursts |

## Ausdruck & Modulation

- **Velocity** aus der Touch-Intensität (Kontaktfläche): steuert
  Lautstärke, beim Piano zusätzlich die Helligkeit, bei den
  Tiefpass-Drums die Rausch-Helligkeit.
- **Aftertouch**: Der Anpressdruck bei gehaltener Note moduliert am
  Lautsprecher die Lautstärke der einzelnen Stimme (`v.pressure`);
  One-Shots (Drums) ignorieren ihn.
- **Arpeggio**: Statt Akkord werden gehaltene Melodie-Stimmen zyklisch
  als schnelle Folge gespielt (Off/Slow/Fast/Turbo, `ARP_STEP_MS`),
  sample-genau im Audio-Task, mit kurzer De-Klick-Blende
  (`ARP_FADE_MS` 3 ms). Drums werden nicht arpeggiert.

## Effekte

Ein globaler Effekt auf die Mix-Summe (Menü, im NVS gespeichert):

- **Delay**: eine Rückkopplungs-Delay-Line (`FX_DELAY_MS` /
  `FX_DELAY_FEEDBACK` / `FX_DELAY_WET`).
- **Reverb**: Freeverb-artig, 8 Kamm- + 4 Allpassfilter (`FX_REVERB_ROOM`
  / `_DAMP` / `_WET`).

Delay und Reverb teilen sich denselben Pufferspeicher (nie gleichzeitig
aktiv); beim Umschalten leert der Audio-Task die Puffer, damit keine alte
Fahne stehen bleibt.
