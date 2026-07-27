#include "MenuController.h"

#include <stdio.h>

#include "Config.h"
#include "DisplayController.h"
#include "Drums.h"
#include "LoopStore.h"
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
    ITEM_SAVE, // aktuellen Loop auf dem Gerät sichern
    ITEM_LOAD, // gespeicherten Loop laden (Drehen wählt aus)
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
    // Erst den reinen Inhalt (Name: Wert bzw. Aktionsname) bauen …
    char body[32];

    switch (_item)
    {
    case ITEM_WAVEFORM:
        snprintf(body, sizeof(body), "Waveform: %s", waveformNames[Settings::waveform()]);
        break;

    case ITEM_ARP:
        snprintf(body, sizeof(body), "Arp: %s", arpNames[Settings::arp()]);
        break;

    case ITEM_FX:
        snprintf(body, sizeof(body), "FX: %s", fxNames[Settings::fx()]);
        break;

    case ITEM_SCALE:
        snprintf(body, sizeof(body), "Scale: %s", scaleNames[Settings::scale()]);
        break;

    case ITEM_OCTAVE:
        snprintf(body, sizeof(body), "Octave: %+d", Settings::octave());
        break;

    case ITEM_MIDI:
        snprintf(body, sizeof(body), "MIDI: %s", Settings::midi() ? "On" : "Off");
        break;

    case ITEM_SAVE:
        snprintf(body, sizeof(body), "Save Loop");
        break;

    case ITEM_LOAD:
        if (LoopStore::count() == 0)
        {
            snprintf(body, sizeof(body), "Load: (leer)");
        }
        else
        {
            snprintf(body, sizeof(body), "Load: %s", LoopStore::name(_loadIndex));
        }
        break;

    case ITEM_CALIBRATE:
        snprintf(body, sizeof(body), "Calibrate");
        break;

    case ITEM_RESET:
        snprintf(body, sizeof(body), "Factory Reset");
        break;

    case ITEM_INSTRUMENT:
    default:
        snprintf(body, sizeof(body), "Sound: %s", instrumentNames[Settings::instrument()]);
        break;
    }

    // … dann je nach Modus einrahmen: Rückfrage beim Reset, eckige
    // Klammern beim Bearbeiten, „> " beim Blättern.
    char text[40];

    if (_item == ITEM_RESET && _resetArmed)
    {
        snprintf(text, sizeof(text), "Reset: click to confirm");
    }
    else if (_editing)
    {
        snprintf(text, sizeof(text), "[%s]", body);
    }
    else
    {
        snprintf(text, sizeof(text), "> %s", body);
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
    if (!_open)
    {
        // Menü öffnet immer bei „Sound", im Blätter-Modus
        _open       = true;
        _editing    = false;
        _resetArmed = false;
        _item       = ITEM_INSTRUMENT;

        show();

        return;
    }

    if (_editing)
    {
        // ITEM_LOAD: der Klick im Bearbeiten-Modus lädt den gewählten
        // Loop (main.cpp führt es aus, es braucht Looper + LoopStore)
        if (_item == ITEM_LOAD)
        {
            _loadRequested = true;

            _open = false;

            return;
        }

        // Bearbeiten beenden — zurück zum Blättern
        _editing = false;

        show();

        return;
    }

    // Blätter-Modus: Klick auf den aktuellen Eintrag
    switch (_item)
    {
    case ITEM_SAVE:
        // Aktion: aktuellen Loop sichern (main.cpp serialisiert + schreibt)
        _saveRequested = true;

        _open = false;

        return;

    case ITEM_LOAD:
        // In den Bearbeiten-Modus zum Auswählen (nur wenn etwas da ist)
        if (LoopStore::count() == 0)
        {
            _open = false;

            return;
        }

        if (_loadIndex >= LoopStore::count())
        {
            _loadIndex = 0;
        }

        _editing = true;

        show();

        return;

    case ITEM_CALIBRATE:
        // Aktion: Kalibrierung anfordern und Menü schließen — main.cpp
        // führt sie aus und übernimmt danach das Display
        _calibrateRequested = true;

        _open = false;

        return;

    case ITEM_RESET:
        // Zweistufig gegen versehentliches Zurücksetzen: erster Klick
        // fragt nach, zweiter führt aus
        if (!_resetArmed)
        {
            _resetArmed = true;

            show();

            return;
        }

        Settings::factoryReset();

        Serial.println("Werkseinstellungen — Neustart");

        delay(200);

        ESP.restart();

        return;

    default:
        // Wert-Parameter: in den Bearbeiten-Modus wechseln
        _editing = true;

        show();

        return;
    }
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

    // Blätter-Modus: Drehen wechselt den Parameter (mit Umlauf)
    if (!_editing)
    {
        int32_t n = (static_cast<int32_t>(_item) + detents) % ITEM_COUNT;

        if (n < 0)
        {
            n += ITEM_COUNT;
        }

        _item = static_cast<uint8_t>(n);

        // Wegblättern von „Factory Reset" entschärft die Rückfrage
        _resetArmed = false;

        show();

        return;
    }

    // ITEM_LOAD: Drehen wählt einen gespeicherten Loop aus (kein Wert)
    if (_item == ITEM_LOAD)
    {
        uint8_t n = LoopStore::count();

        if (n > 0)
        {
            int32_t sel = (static_cast<int32_t>(_loadIndex) + detents) % n;

            if (sel < 0)
            {
                sel += n;
            }

            _loadIndex = static_cast<uint8_t>(sel);
        }

        show();

        return;
    }

    // Bearbeiten-Modus: Drehen ändert den Wert des aktuellen Parameters
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

        // ITEM_CALIBRATE und ITEM_RESET sind Aktionen ohne Wert — sie
        // laufen über handleClick(), im Bearbeiten-Modus landet man hier
        // gar nicht.
    }

    markDirty();

    show();
}

void MenuController::update()
{
    if (_open && static_cast<int32_t>(millis() - _deadline) >= 0)
    {
        _open = false;

        // Beim Timeout Bearbeiten-Modus verlassen und eine angefangene
        // Reset-Rückfrage entschärfen
        _editing    = false;
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

bool MenuController::takeSaveRequest()
{
    bool r = _saveRequested;

    _saveRequested = false;

    return r;
}

bool MenuController::takeLoadRequest(uint8_t& index)
{
    bool r = _loadRequested;

    _loadRequested = false;

    if (r)
    {
        index = _loadIndex;
    }

    return r;
}