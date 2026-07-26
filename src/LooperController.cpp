#include "LooperController.h"

#include "DisplayController.h"
#include "MidiController.h"
#include "SpeakerController.h"

void LooperController::begin(SpeakerController* speaker, MidiController* midi,
                             DisplayController* display)
{
    _speaker = speaker;
    _midi    = midi;
    _display = display;
}

void LooperController::setState(State s, const char* label)
{
    _state = s;

    if (_display != nullptr)
    {
        _display->showToast(label);
    }
}

// ------------------------------------------------
// Wiedergabe der klingenden Loop-Noten verwalten
// ------------------------------------------------

void LooperController::forceOffActive()
{
    for (uint8_t i = 0; i < _activeCount; i++)
    {
        const ActiveNote& a = _active[i];

        if (a.viaMidi)
        {
            _midi->noteOff(a.note, a.channel);
        }
        else
        {
            _speaker->noteOff(a.note);
        }
    }

    _activeCount = 0;
}

void LooperController::fireEvent(const LoopEvent& e)
{
    if (e.velocity > 0)
    {
        // NoteOn — an die aufgenommene Senke
        if (e.viaMidi)
        {
            _midi->noteOn(e.note, e.velocity, e.channel);
        }
        else
        {
            _speaker->noteOnAs(e.instrument, e.note, e.velocity);
        }

        // Als klingend vermerken (für Force-Off am Loop-Ende); volle
        // Liste: ältesten Eintrag verwerfen, damit nichts überläuft
        if (_activeCount < LOOP_MAX_ACTIVE)
        {
            _active[_activeCount++] = {e.note, e.channel, e.viaMidi};
        }
    }
    else
    {
        // NoteOff
        if (e.viaMidi)
        {
            _midi->noteOff(e.note, e.channel);
        }
        else
        {
            _speaker->noteOff(e.note);
        }

        // Aus der Aktiv-Liste entfernen
        for (uint8_t i = 0; i < _activeCount; i++)
        {
            if (_active[i].note == e.note && _active[i].viaMidi == e.viaMidi)
            {
                _active[i] = _active[--_activeCount];

                break;
            }
        }
    }
}

void LooperController::fireWindow(uint32_t from, uint32_t to)
{
    // Alle Events feuern, deren Zeit im halb-offenen Fenster [from, to)
    // liegt. Die Events sind nicht sortiert (Overdub hängt hinten an),
    // deshalb ein linearer Durchlauf — bei ein paar hundert Events und
    // dem 5-ms-Loop-Takt problemlos.
    for (uint16_t i = 0; i < _count; i++)
    {
        if (_events[i].timeMs >= from && _events[i].timeMs < to)
        {
            fireEvent(_events[i]);
        }
    }
}

// ------------------------------------------------
// Bedienung
// ------------------------------------------------

void LooperController::toggleRecord()
{
    switch (_state)
    {
    case EMPTY:
        _count    = 0;
        _recStart = millis();
        setState(REC, "REC");
        break;

    case REC:
    {
        // Erstaufnahme abschließen: Loop-Länge = Aufnahmedauer
        uint32_t len = millis() - _recStart;

        _loopLen = len < LOOP_MIN_MS ? LOOP_MIN_MS : len;

        _loopStart   = millis();
        _lastPos     = 0;
        _activeCount = 0;

        setState(PLAY, "PLAY");
        break;
    }

    case PLAY:
        setState(OVERDUB, "OVERDUB");
        break;

    case OVERDUB:
        setState(PLAY, "PLAY");
        break;

    case STOPPED:
        // Wiedergabe von vorn fortsetzen
        _loopStart = millis();
        _lastPos   = 0;
        setState(PLAY, "PLAY");
        break;
    }
}

void LooperController::toggleStop()
{
    switch (_state)
    {
    case PLAY:
    case OVERDUB:
        forceOffActive();
        setState(STOPPED, "STOP");
        break;

    case STOPPED:
        _loopStart = millis();
        _lastPos   = 0;
        setState(PLAY, "PLAY");
        break;

    case REC:
        // laufende Erstaufnahme verwerfen
        _count = 0;
        setState(EMPTY, "CANCEL");
        break;

    case EMPTY:
        break;
    }
}

void LooperController::clear()
{
    forceOffActive();

    _count   = 0;
    _loopLen = 0;

    setState(EMPTY, "CLEAR");
}

// ------------------------------------------------
// Aufnahme & Wiedergabe
// ------------------------------------------------

void LooperController::recordEvent(bool on, uint8_t note, uint8_t velocity, uint8_t instrument,
                                   bool viaMidi, uint8_t channel)
{
    if (_state != REC && _state != OVERDUB)
    {
        return;
    }

    if (_count >= LOOP_MAX_EVENTS)
    {
        return; // Puffer voll — weitere Events verwerfen
    }

    uint32_t t = _state == REC ? (millis() - _recStart) : ((millis() - _loopStart) % _loopLen);

    _events[_count++] = {t,          note,    static_cast<uint8_t>(on ? velocity : 0),
                         instrument, channel, viaMidi};
}

void LooperController::update()
{
    if (_state != PLAY && _state != OVERDUB)
    {
        return;
    }

    uint32_t pos  = (millis() - _loopStart) % _loopLen;
    uint32_t prev = _lastPos;

    _lastPos = pos;

    if (pos >= prev)
    {
        fireWindow(prev, pos);
    }
    else
    {
        // Loop-Umlauf: Rest bis zum Ende, hängende Noten beenden, dann
        // von vorn — so bleibt keine Note über die Nahtstelle hängen
        fireWindow(prev, _loopLen);

        forceOffActive();

        fireWindow(0, pos);
    }
}

bool LooperController::active() const
{
    return _state == REC || _state == PLAY || _state == OVERDUB;
}

uint8_t LooperController::displayState() const
{
    if (_state == REC || _state == OVERDUB)
    {
        return 2; // Aufnahme
    }

    if (_state == PLAY)
    {
        return 1; // Wiedergabe
    }

    return 0; // aus/gestoppt
}
