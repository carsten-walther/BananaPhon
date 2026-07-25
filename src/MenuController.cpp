#include "MenuController.h"

#include <stdio.h>

#include "Config.h"
#include "DisplayController.h"
#include "Drums.h"
#include "Scales.h"
#include "Settings.h"
#include "SpeakerController.h"

namespace
{
enum Item : uint8_t
{
    ITEM_INSTRUMENT = 0,
    ITEM_WAVEFORM,
    ITEM_ARP,
    ITEM_FX,
    ITEM_SCALE,
    ITEM_OCTAVE,
    ITEM_MIDI,
    ITEM_CALIBRATE,
    ITEM_RESET,

    ITEM_COUNT
};

// Ohne Umlaute — die geladene DejaVu-Schrift deckt ASCII sicher ab
const char* waveformNames[WAVE_COUNT] = {"Triangle", "Rectangle", "Saw", "Sine", "8-Bit"};

const char* arpNames[ARP_MODE_COUNT] = {"Off", "Slow", "Fast", "Turbo"};

const char* fxNames[FX_COUNT] = {"Off", "Delay", "Reverb"};
} // namespace

void MenuController::begin(SpeakerController* speaker, DisplayController* display)
{
    _speaker = speaker;
    _display = display;
}

void MenuController::show()
{
    char text[32];

    switch (_item)
    {
    case ITEM_WAVEFORM:
        snprintf(text, sizeof(text), "Waveform: %s", waveformNames[Settings::waveform()]);
        break;


    case ITEM_ARP:
        snprintf(text, sizeof(text), "Arp: %s", arpNames[Settings::arp()]);
        break;

    case ITEM_FX:
        snprintf(text, sizeof(text), "FX: %s", fxNames[Settings::fx()]);
        break;

    case ITEM_SCALE:
        snprintf(text, sizeof(text), "Scale: %s", scaleNames[Settings::scale()]);
        break;

    case ITEM_OCTAVE:
        snprintf(text, sizeof(text), "Octave: %+d", Settings::octave());
        break;

    case ITEM_MIDI:
        snprintf(text, sizeof(text), "MIDI: %s", Settings::midi() ? "On" : "Off");
        break;

    case ITEM_CALIBRATE:
        // Aktion statt Wert: Drehen löst die Kalibrierung aus
        snprintf(text, sizeof(text), "Calibrate: turn");
        break;

    case ITEM_RESET:
        // Zweistufig gegen versehentliches Zurücksetzen
        snprintf(text, sizeof(text), _resetArmed ? "Reset: sure? turn" : "Factory Reset");
        break;

    case ITEM_INSTRUMENT:
    default:
        snprintf(text, sizeof(text), "Sound: %s", instrumentNames[Settings::instrument()]);
        break;
    }

    _display->showToast(text, MENU_TIMEOUT_MS);

    _deadline = millis() + MENU_TIMEOUT_MS;
}

void MenuController::applyVolume(int32_t detents)
{
    float v = Settings::volume() + detents * ENCODER_VOLUME_STEP;

    if (v < 0.0f)
    {
        v = 0.0f;
    }

    if (v > 1.0f)
    {
        v = 1.0f;
    }

    Settings::setVolume(v);

    _speaker->setVolume(v);
}

void MenuController::markDirty()
{
    _dirty  = true;
    _saveAt = millis() + 2000;
}

void MenuController::handleClick()
{
    // Jeder Klick entschärft einen angefangenen Werksreset (Wechsel
    // des Parameters oder Öffnen des Menüs)
    _resetArmed = false;

    if (!_open)
    {
        // Menü öffnet auf dem zuletzt benutzten Parameter — so ist
        // z. B. der Sound-Wechsel nur noch einen Klick entfernt
        _open = true;
    }
    else
    {
        _item = (_item + 1) % ITEM_COUNT;
    }

    show();
}

void MenuController::handleRotation(int32_t detents)
{
    // Geschlossen: Drehen = Lautstärke-Schnellzugriff
    if (!_open)
    {
        applyVolume(detents);

        markDirty();

        char text[16];

        snprintf(text, sizeof(text), "Volume: %d%%",
                 static_cast<int>(Settings::volume() * 100.0f + 0.5f));

        _display->showToast(text);

        return;
    }

    switch (_item)
    {
    case ITEM_WAVEFORM:
    {
        int32_t wf = (Settings::waveform() + detents) % WAVE_COUNT;

        if (wf < 0)
        {
            wf += WAVE_COUNT;
        }

        Settings::setWaveform(static_cast<uint8_t>(wf));

        _speaker->setWaveform(static_cast<uint8_t>(wf));

        break;
    }

    case ITEM_INSTRUMENT:
    default:
    {
        int32_t in = (Settings::instrument() + detents) % INST_COUNT;

        if (in < 0)
        {
            in += INST_COUNT;
        }

        Settings::setInstrument(static_cast<uint8_t>(in));

        _speaker->setInstrument(static_cast<uint8_t>(in));

        // Tastenbeschriftung wechselt zwischen Noten und Drum-Kürzeln
        _display->setInstrument(static_cast<uint8_t>(in));

        _display->showPads();

        break;
    }

    case ITEM_ARP:
    {
        int32_t am = (Settings::arp() + detents) % ARP_MODE_COUNT;

        if (am < 0)
        {
            am += ARP_MODE_COUNT;
        }

        Settings::setArp(static_cast<uint8_t>(am));

        _speaker->setArp(static_cast<uint8_t>(am));

        break;
    }

    case ITEM_FX:
    {
        int32_t fx = (Settings::fx() + detents) % FX_COUNT;

        if (fx < 0)
        {
            fx += FX_COUNT;
        }

        Settings::setFx(static_cast<uint8_t>(fx));

        _speaker->setFx(static_cast<uint8_t>(fx));

        break;
    }

    case ITEM_SCALE:
    {
        int32_t sc = (Settings::scale() + detents) % SCALE_COUNT;

        if (sc < 0)
        {
            sc += SCALE_COUNT;
        }

        Settings::setScale(static_cast<uint8_t>(sc));

        // Tastenbeschriftung folgt der neuen Skala
        _display->setScale(static_cast<uint8_t>(sc));

        _display->showPads();

        break;
    }

    case ITEM_OCTAVE:
    {
        int32_t oct = Settings::octave() + detents;

        if (oct < -OCTAVE_RANGE)
        {
            oct = -OCTAVE_RANGE;
        }

        if (oct > OCTAVE_RANGE)
        {
            oct = OCTAVE_RANGE;
        }

        Settings::setOctave(static_cast<int8_t>(oct));

        // Tastenbeschriftung zieht sofort mit
        _display->setOctave(static_cast<int8_t>(oct));

        _display->showPads();

        break;
    }

    case ITEM_MIDI:
    {
        // Zwei Zustände: rechts drehen = An, links drehen = Aus.
        // Das NoteOff folgt der beim NoteOn gemerkten Senke, ein
        // Umschalten während einer gehaltenen Note lässt sie also nicht
        // hängen (siehe noteViaMidi[] in main.cpp).
        Settings::setMidi(detents > 0);

        break;
    }

    case ITEM_CALIBRATE:
    {
        // Aktion, kein Wert: Kalibrierung anfordern und das Menü sofort
        // schließen — main.cpp führt sie aus (übernimmt danach das
        // Display). Nichts zu speichern, daher kein markDirty()/show().
        _calibrateRequested = true;

        _open = false;

        return;
    }

    case ITEM_RESET:
    {
        // Zweistufig: erste Drehung schärft, zweite führt aus. So löst
        // ein versehentliches Antippen keinen Werksreset aus.
        if (!_resetArmed)
        {
            _resetArmed = true;

            show(); // zeigt jetzt die Rückfrage, hält das Menü offen

            return;
        }

        Settings::factoryReset();

        Serial.println("Werkseinstellungen — Neustart");

        delay(200);

        ESP.restart();

        return;
    }
    }

    markDirty();

    show();
}

void MenuController::update()
{
    if (_open && static_cast<int32_t>(millis() - _deadline) >= 0)
    {
        _open = false;

        // Menü-Timeout entschärft einen angefangenen Werksreset
        _resetArmed = false;
    }

    // Verzögert speichern: gebündelt 2 s nach der letzten Änderung,
    // und nur bei geschlossenem Menü — schont den NVS-Flash
    if (_dirty && !_open && static_cast<int32_t>(millis() - _saveAt) >= 0)
    {
        _dirty = false;

        Settings::save();

        Serial.println("Einstellungen gespeichert");
    }
}

bool MenuController::takeCalibrateRequest()
{
    bool r = _calibrateRequested;

    _calibrateRequested = false;

    return r;
}