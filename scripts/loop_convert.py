#!/usr/bin/env python3
"""Konvertiert BananaPhon-Loops zwischen dem Binärformat (.loop, "BPL1")
und einem editierbaren Textformat. Die Richtung wird am Inhalt erkannt.

    scripts/loop_convert.py foo.loop            # binär -> Text (nach stdout)
    scripts/loop_convert.py foo.loop foo.txt    # binär -> Text (in Datei)
    scripts/loop_convert.py foo.txt             # Text -> binär (foo.loop)
    scripts/loop_convert.py foo.txt foo.loop    # Text -> binär (in Datei)

Textformat (Zeilen; '#' und Leerzeilen werden ignoriert):
    loop <ms>                       # Loop-Länge, einmal am Anfang
    <time> <note> <vel> <inst> [ch] [midi]
      time  = Offset in ms
      note  = Notenname (H3, D#4 – deutsch, H=B) ODER Drum-Kürzel
              (KD SN HH OH T1 T2 CP) ODER MIDI-Nummer
      vel   = Velocity 0..127  (0 = NoteOff)
      inst  = piano | drums | chip
      ch    = MIDI-Kanal (optional; Default 10 für drums, sonst 1)
      midi  = Wort "midi", wenn das Event über MIDI statt Speaker geht
              (optional; Default Speaker)

Die Konstanten unten spiegeln include/Drums.h und include/Config.h —
bei Änderungen dort mitziehen.
"""
import os
import re
import struct
import sys

# --- muss mit der Firmware übereinstimmen ---
INST_NAMES = {0: "piano", 1: "drums", 2: "chip"}  # Instrument-Enum (Drums.h)
INST_IDS = {v: k for k, v in INST_NAMES.items()}
INST_DRUMS = 1
DRUM = {"KD": 36, "SN": 38, "HH": 42, "OH": 46, "T1": 45, "T2": 50, "CP": 39}  # drumNotes[]
DRUM_REV = {v: k for k, v in DRUM.items()}
NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "H"]  # deutsch
NAME_TO_SEMI = {n: i for i, n in enumerate(NOTE_NAMES)}
NAME_TO_SEMI["B"] = 11  # englisch B = deutsch H

MAGIC = b"BPL1"
LOOP_HEADER = 10
LOOP_EVENT = 9
LOOP_MAX_EVENTS = 512
LOOP_MIN_MS = 200


def note_to_name(n):
    return f"{NOTE_NAMES[n % 12]}{n // 12 - 1}"


def parse_note(tok):
    up = tok.upper()
    if up in DRUM:
        return DRUM[up]
    try:
        return int(tok)
    except ValueError:
        pass
    # Notenname: <Name><Oktave>, z. B. D#4, H3, C-1
    i = 0
    while i < len(up) and up[i] not in "0123456789-":
        i += 1
    name, octave = up[:i], up[i:]
    if name not in NAME_TO_SEMI or octave in ("", "-"):
        raise ValueError(f"unbekannte Note '{tok}'")
    return (int(octave) + 1) * 12 + NAME_TO_SEMI[name]


def decode(raw):
    if raw[:4] != MAGIC:
        raise ValueError("kein BPL1-Loop")
    loop_len, count = struct.unpack_from("<IH", raw, 4)
    if len(raw) < LOOP_HEADER + count * LOOP_EVENT:
        raise ValueError("Datei abgeschnitten")

    lines = [
        "# BananaPhon-Loop (Text) — mit scripts/loop_convert.py zurück nach .loop",
        f"loop {loop_len}",
        "# time  note  vel  inst  [ch] [midi]",
    ]
    p = LOOP_HEADER
    for _ in range(count):
        ts, note, vel, inst, ch, via = struct.unpack_from("<IBBBBB", raw, p)
        p += LOOP_EVENT
        if inst == INST_DRUMS and note in DRUM_REV:
            nstr = DRUM_REV[note]
        else:
            nstr = note_to_name(note)
        line = f"{ts:6} {nstr:5} {vel:4} {INST_NAMES.get(inst, inst):6} {ch}"
        if via:
            line += " midi"
        lines.append(line)
    return "\n".join(lines) + "\n"


def encode(text):
    loop_len = None
    events = []
    for lineno, raw_line in enumerate(text.splitlines(), 1):
        # '#' ist nur Kommentar am Zeilenanfang oder mit Leerzeichen davor
        # — sonst würde es das '#' in Notennamen wie D#4 zerschneiden.
        if raw_line.lstrip().startswith("#"):
            continue
        line = re.split(r"\s#", raw_line, maxsplit=1)[0].strip()
        if not line:
            continue
        f = line.split()
        if f[0].lower() in ("loop", "looplen"):
            loop_len = int(f[1])
            continue
        if len(f) < 4:
            raise ValueError(f"Zeile {lineno}: erwartet 'time note vel inst [ch] [midi]'")
        try:
            time = int(f[0])
            note = parse_note(f[1])
            vel = int(f[2])
            inst = INST_IDS[f[3].lower()]
        except (KeyError, ValueError) as e:
            raise ValueError(f"Zeile {lineno}: {e}") from None
        via = 0
        ch = 10 if inst == INST_DRUMS else 1
        for extra in f[4:]:
            if extra.lower() in ("midi", "speaker"):
                via = 1 if extra.lower() == "midi" else 0
            else:
                ch = int(extra)
        for name, val in (("note", note), ("vel", vel), ("ch", ch)):
            if not 0 <= val <= (127 if name != "ch" else 16):
                raise ValueError(f"Zeile {lineno}: {name}={val} außerhalb des Bereichs")
        events.append((time, note, vel, inst, ch, via))

    if loop_len is None:
        raise ValueError("keine 'loop <ms>'-Zeile gefunden")
    if loop_len < LOOP_MIN_MS:
        raise ValueError(f"loop {loop_len} ms < Minimum {LOOP_MIN_MS} ms")
    if not events:
        raise ValueError("keine Events")
    if len(events) > LOOP_MAX_EVENTS:
        raise ValueError(f"{len(events)} Events > Maximum {LOOP_MAX_EVENTS}")
    late = [e for e in events if e[0] >= loop_len]
    if late:
        raise ValueError(f"{len(late)} Event(s) liegen bei/nach loop {loop_len} ms und würden nie spielen")

    buf = bytearray(MAGIC)
    buf += struct.pack("<IH", loop_len, len(events))
    for ts, note, vel, inst, ch, via in events:
        buf += struct.pack("<IBBBBB", ts, note, vel, inst, ch, via)
    return bytes(buf)


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    inp = argv[1]
    out = argv[2] if len(argv) > 2 else None
    raw = open(inp, "rb").read()

    if raw[:4] == MAGIC:
        text = decode(raw)
        if out:
            open(out, "w", encoding="utf-8").write(text)
            print(f"geschrieben: {out} ({len(text.splitlines())} Zeilen)")
        else:
            sys.stdout.write(text)
    else:
        blob = encode(raw.decode("utf-8"))
        if not out:
            out = os.path.splitext(inp)[0] + ".loop"
        open(out, "wb").write(blob)
        print(f"geschrieben: {out} ({len(blob)} Byte)")


if __name__ == "__main__":
    try:
        main(sys.argv)
    except (ValueError, KeyError, OSError) as e:
        sys.exit(f"Fehler: {e}")
