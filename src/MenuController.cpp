#include "MenuController.h"

#include <stdio.h>

#include "Config.h"
#include "DisplayController.h"
#include "Scales.h"
#include "Settings.h"
#include "SpeakerController.h"

namespace
{
enum Item : uint8_t
{
    ITEM_VOLUME = 0,
    ITEM_WAVEFORM,
    ITEM_SCALE,
    ITEM_OCTAVE,

    ITEM_COUNT
};

// Ohne Umlaute — die geladene DejaVu-Schrift deckt ASCII sicher ab
const char* waveformNames[WAVE_COUNT] = {"Triangle", "Rectangle", "Saw", "Sine"};
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

    case ITEM_OCTAVE:
        snprintf(text, sizeof(text), "Octave: %+d", Settings::octave());
        break;

    case ITEM_VOLUME:
    default:
        snprintf(text, sizeof(text), "Volume: %d%%",
                 static_cast<int>(Settings::volume() * 100.0f + 0.5f));
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
    if (!_open)
    {
        _open = true;
        _item = ITEM_VOLUME;
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

    case ITEM_VOLUME:
    default:
        applyVolume(detents);
        break;
    }

    markDirty();

    show();
}

void MenuController::update()
{
    if (_open && static_cast<int32_t>(millis() - _deadline) >= 0)
    {
        _open = false;
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
