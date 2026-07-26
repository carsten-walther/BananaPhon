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

// ------------------------------------------------
// Serialisierung (Browser-Austausch)
// ------------------------------------------------
//
// Kompaktes, plattform-explizit little-endian gepacktes Format:
//   "BPL1"            4 Byte Magic + Version
//   loopLen           uint32
//   count             uint16
//   count × Event     je 9 Byte: timeMs(u32) note vel instrument channel viaMidi
namespace
{
constexpr uint8_t LOOP_MAGIC[4] = {'B', 'P', 'L', '1'};
constexpr size_t LOOP_HEADER    = 4 + 4 + 2;
constexpr size_t LOOP_EVENT     = 9;

void putU32(uint8_t* b, size_t& p, uint32_t v)
{
    b[p++] = v & 0xFF;
    b[p++] = (v >> 8) & 0xFF;
    b[p++] = (v >> 16) & 0xFF;
    b[p++] = (v >> 24) & 0xFF;
}

uint32_t getU32(const uint8_t* b, size_t p)
{
    return static_cast<uint32_t>(b[p]) | (static_cast<uint32_t>(b[p + 1]) << 8) |
           (static_cast<uint32_t>(b[p + 2]) << 16) | (static_cast<uint32_t>(b[p + 3]) << 24);
}
} // namespace

size_t LooperController::serialize(uint8_t* buf, size_t max) const
{
    if (_count == 0)
    {
        return 0; // nichts aufgenommen
    }

    size_t need = LOOP_HEADER + static_cast<size_t>(_count) * LOOP_EVENT;

    if (need > max)
    {
        return 0; // Puffer zu klein
    }

    size_t p = 0;

    buf[p++] = LOOP_MAGIC[0];
    buf[p++] = LOOP_MAGIC[1];
    buf[p++] = LOOP_MAGIC[2];
    buf[p++] = LOOP_MAGIC[3];

    putU32(buf, p, _loopLen);

    buf[p++] = _count & 0xFF;
    buf[p++] = (_count >> 8) & 0xFF;

    for (uint16_t i = 0; i < _count; i++)
    {
        const LoopEvent& e = _events[i];

        putU32(buf, p, e.timeMs);

        buf[p++] = e.note;
        buf[p++] = e.velocity;
        buf[p++] = e.instrument;
        buf[p++] = e.channel;
        buf[p++] = e.viaMidi ? 1 : 0;
    }

    return p;
}

bool LooperController::deserialize(const uint8_t* buf, size_t len)
{
    if (len < LOOP_HEADER)
    {
        return false;
    }

    if (buf[0] != LOOP_MAGIC[0] || buf[1] != LOOP_MAGIC[1] || buf[2] != LOOP_MAGIC[2] ||
        buf[3] != LOOP_MAGIC[3])
    {
        return false; // falsches Format / falsche Version
    }

    uint32_t loopLen = getU32(buf, 4);
    uint16_t count   = static_cast<uint16_t>(buf[8] | (buf[9] << 8));

    if (loopLen < LOOP_MIN_MS || count == 0 || count > LOOP_MAX_EVENTS)
    {
        return false;
    }

    if (len < LOOP_HEADER + static_cast<size_t>(count) * LOOP_EVENT)
    {
        return false; // abgeschnitten
    }

    // Laufende Wiedergabe sauber beenden, dann den neuen Loop übernehmen
    forceOffActive();

    size_t p = LOOP_HEADER;

    for (uint16_t i = 0; i < count; i++)
    {
        _events[i].timeMs = getU32(buf, p);
        p += 4;
        _events[i].note       = buf[p++];
        _events[i].velocity   = buf[p++];
        _events[i].instrument = buf[p++];
        _events[i].channel    = buf[p++];
        _events[i].viaMidi    = buf[p++] != 0;
    }

    _count     = count;
    _loopLen   = loopLen;
    _loopStart = millis();
    _lastPos   = 0;

    setState(PLAY, "Loop geladen");

    return true;
}
