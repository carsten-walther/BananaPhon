#include "DisplayController.h"

#include "Config.h"

#include <LovyanGFX.hpp>

// ------------------------------------------------
// LilyGO T-Display S3 (ST7789, 8-Bit-Parallelbus)
// ------------------------------------------------

// LCD-Spannungsversorgung des Boards
constexpr uint8_t PIN_LCD_POWER = 15;

class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_Parallel8 _bus;
    lgfx::Light_PWM _light;

public:
    LGFX()
    {
        {
            auto cfg = _bus.config();

            cfg.freq_write = 20000000;

            cfg.pin_wr = 8;
            cfg.pin_rd = 9;
            cfg.pin_rs = 7; // DC

            cfg.pin_d0 = 39;
            cfg.pin_d1 = 40;
            cfg.pin_d2 = 41;
            cfg.pin_d3 = 42;
            cfg.pin_d4 = 45;
            cfg.pin_d5 = 46;
            cfg.pin_d6 = 47;
            cfg.pin_d7 = 48;

            _bus.config(cfg);

            _panel.setBus(&_bus);
        }

        {
            auto cfg = _panel.config();

            cfg.pin_cs   = 6;
            cfg.pin_rst  = 5;
            cfg.pin_busy = -1;

            cfg.panel_width  = 170;
            cfg.panel_height = 320;

            cfg.offset_x = 35;
            cfg.offset_y = 0;

            cfg.readable   = true;
            cfg.invert     = true;
            cfg.rgb_order  = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;

            _panel.config(cfg);
        }

        {
            auto cfg = _light.config();

            cfg.pin_bl      = 38;
            cfg.invert      = false;
            cfg.freq        = 44100;
            cfg.pwm_channel = 7;

            _light.config(cfg);

            _panel.setLight(&_light);
        }

        setPanel(&_panel);
    }
};

static LGFX display;

// ------------------------------------------------
// Layout
// ------------------------------------------------

constexpr int32_t TITLE_Y = 10;

constexpr int32_t STATUS_Y      = 40;
constexpr int32_t STATUS_HEIGHT = 20;

constexpr int32_t PAD_Y      = 70;
constexpr int32_t PAD_HEIGHT = 90;
constexpr int32_t PAD_GAP    = 4;

// Batterie-Symbol oben rechts
constexpr int32_t BAT_WIDTH  = 26;
constexpr int32_t BAT_HEIGHT = 12;
constexpr int32_t BAT_NUB    = 3;
constexpr int32_t BAT_MARGIN = 6;

// Pad-Innenfläche (2 px Rand innerhalb der abgerundeten Grundfläche)
constexpr int32_t PAD_INNER = PAD_HEIGHT - 4;

// Notenname unten im Pad: Textmitte, gemessen ab Pad-Unterkante
constexpr int32_t PAD_LABEL_CENTER = 16;

// Bereich ab Unterkante, in dem der Notenname liegt — läuft der
// Marker hindurch, muss der Text neu gezeichnet werden
constexpr int32_t PAD_LABEL_ZONE = 26;

constexpr int32_t PEAK_MARKER_H = 3;

// LiPo-Spannungsgrenzen für die Prozentrechnung
constexpr uint32_t BAT_EMPTY_MV = 3300;
constexpr uint32_t BAT_FULL_MV  = 4200;

// Darüber liegt USB-Versorgung an (kein sinnvoller Batteriewert)
constexpr uint32_t USB_POWER_MV = 4400;

// ------------------------------------------------
// Helfer
// ------------------------------------------------

// Schreibt den Notennamen (z. B. "C4") nach `buf`. Der Puffer kommt vom
// Aufrufer — kein statischer Zustand, die Funktion ist reentrant.
static void noteName(uint8_t note, char* buf, size_t len)
{
    static const char* namesEn[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                                      "F#", "G",  "G#", "A",  "A#", "B"};

    // Deutsche Konvention: H statt B (das deutsche B wäre engl. A#/Bb)
    static const char* namesDe[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                                      "F#", "G",  "G#", "A",  "A#", "H"};

    const char** names = USE_GERMAN_NOTE_NAMES ? namesDe : namesEn;

    snprintf(buf, len, "%s%d", names[note % 12], note / 12 - 1);
}

// Farbrampe der Velocity-Anzeige (VU-Stil)
static uint16_t velocityColor(uint8_t velocity)
{
    return velocity < 60 ? TFT_GREEN : (velocity < 100 ? TFT_YELLOW : TFT_RED);
}

// Zeichnet den Notennamen unten ins Pad. `onFill` = Text liegt auf der
// hellen Velocity-Füllung (dann schwarz für den Kontrast).
static void drawPadLabel(uint8_t index, int32_t x, int32_t padWidth, bool onFill)
{
    char label[8];

    noteName(midiNotes[index], label, sizeof(label));

    display.setTextSize(2);

    display.setTextDatum(textdatum_t::middle_center);

    display.setTextColor(onFill ? TFT_BLACK : TFT_WHITE);

    display.drawString(label, x + padWidth / 2, PAD_Y + PAD_HEIGHT - PAD_LABEL_CENTER);
}

// Zeichnet die Peak-Markerlinie auf Höhe `pos` (px ab Pad-Unterkante)
static void drawPeakMarker(int32_t x, int32_t padWidth, int32_t pos, uint8_t velocity)
{
    display.fillRect(x + 2, PAD_Y + 2 + (PAD_INNER - pos), padWidth - 4, PEAK_MARKER_H,
                     velocityColor(velocity));
}

static void clearStatusLine()
{
    display.fillRect(0, STATUS_Y, display.width(), STATUS_HEIGHT, TFT_BLACK);
}

// ------------------------------------------------
// DisplayController
// ------------------------------------------------

void DisplayController::begin()
{
    // LCD-Versorgung einschalten (muss vor display.init() passieren)
    pinMode(PIN_LCD_POWER, OUTPUT);

    digitalWrite(PIN_LCD_POWER, HIGH);

    display.init();

    display.setRotation(1);

    display.setBrightness(255);

    display.fillScreen(TFT_BLACK);

    display.setTextSize(2);

    display.setTextDatum(textdatum_t::top_left);

    display.setTextColor(TFT_WHITE);

    display.drawString("MIDI-Device", 10, TITLE_Y);
}

void DisplayController::showCalibrating()
{
    clearStatusLine();

    display.setTextSize(2);

    display.setTextDatum(textdatum_t::top_left);

    display.setTextColor(TFT_WHITE);

    display.drawString("Kalibriere Touch...", 10, STATUS_Y);
}

void DisplayController::showPads()
{
    clearStatusLine();

    _statusDrawn = false;

    // Neustart/Rekalibrierung: alte Peak-Hold-Marker verwerfen
    for (uint8_t i = 0; i < NUM_SENSORS; i++)
    {
        _peakPos[i]    = 0;
        _padPressed[i] = false;
    }

    for (uint8_t i = 0; i < NUM_SENSORS; i++)
    {
        drawPad(i, false);
    }
}

void DisplayController::drawPad(uint8_t index, bool pressed, uint8_t velocity)
{
    int32_t padWidth = (display.width() - (NUM_SENSORS + 1) * PAD_GAP) / NUM_SENSORS;

    int32_t x = PAD_GAP + index * (padWidth + PAD_GAP);

    _padPressed[index] = pressed;

    // Grundfläche (Ruhezustand); Füllstand/Marker werden darüber gezeichnet
    display.fillRoundRect(x, PAD_Y, padWidth, PAD_HEIGHT, 6, TFT_DARKGREY);

    int32_t fill = 0;

    if (pressed)
    {
        // Füllstand von unten, Höhe und Farbe nach Velocity (VU-Stil).
        fill = PAD_INNER * velocity / 127;

        if (fill > 0)
        {
            display.fillRect(x + 2, PAD_Y + 2 + (PAD_INNER - fill), padWidth - 4, fill,
                             velocityColor(velocity));
        }

        // Peak-Marker auf Füllhöhe "aufziehen" und Haltezeit starten —
        // sichtbar wird er erst nach dem Loslassen.
        _peakPos[index]       = fill < PEAK_MARKER_H ? PEAK_MARKER_H : fill;
        _peakVelocity[index]  = velocity;
        _peakHoldUntil[index] = millis() + VELOCITY_PEAK_HOLD_MS;
    }
    else if (ENABLE_VELOCITY_PEAK_HOLD && _peakPos[index] > 0)
    {
        drawPeakMarker(x, padWidth, _peakPos[index], _peakVelocity[index]);
    }

    // Notenname unten im Pad; auf der Füllung schwarz für den Kontrast
    drawPadLabel(index, x, padWidth, pressed && fill >= PAD_LABEL_ZONE);
}

void DisplayController::updatePeaks()
{
    if (!ENABLE_VELOCITY_PEAK_HOLD)
    {
        return;
    }

    uint32_t now = millis();

    if (now - _lastPeakStep < VELOCITY_PEAK_FALL_INTERVAL_MS)
    {
        return;
    }

    _lastPeakStep = now;

    int32_t padWidth = (display.width() - (NUM_SENSORS + 1) * PAD_GAP) / NUM_SENSORS;

    for (uint8_t i = 0; i < NUM_SENSORS; i++)
    {
        // Nur losgelassene Pads mit Marker, deren Haltezeit abgelaufen
        // ist (Differenz-Vergleich: robust gegen millis()-Überlauf)
        if (_padPressed[i] || _peakPos[i] <= 0 || static_cast<int32_t>(now - _peakHoldUntil[i]) < 0)
        {
            continue;
        }

        int32_t x = PAD_GAP + i * (padWidth + PAD_GAP);

        // Alten Marker löschen
        display.fillRect(x + 2, PAD_Y + 2 + (PAD_INNER - _peakPos[i]), padWidth - 4, PEAK_MARKER_H,
                         TFT_DARKGREY);

        int32_t oldPos = _peakPos[i];

        _peakPos[i] -= VELOCITY_PEAK_FALL_PX;

        if (_peakPos[i] >= PEAK_MARKER_H)
        {
            drawPeakMarker(x, padWidth, _peakPos[i], _peakVelocity[i]);
        }
        else
        {
            _peakPos[i] = 0; // unten angekommen — Marker verschwindet
        }

        // Läuft der Marker durch den Notennamen-Bereich, Text auffrischen
        if (oldPos <= PAD_LABEL_ZONE || _peakPos[i] <= PAD_LABEL_ZONE)
        {
            drawPadLabel(i, x, padWidth, false);
        }
    }
}

void DisplayController::showBattery(uint32_t milliVolts)
{
    bool usbPower = milliVolts > USB_POWER_MV;

    int percent = 0;

    if (milliVolts >= BAT_FULL_MV)
    {
        percent = 100;
    }
    else if (milliVolts > BAT_EMPTY_MV)
    {
        percent = (milliVolts - BAT_EMPTY_MV) * 100 / (BAT_FULL_MV - BAT_EMPTY_MV);
    }

    if (usbPower == _lastUsbPower && (usbPower || percent == _lastBatPercent))
    {
        return;
    }

    _lastUsbPower   = usbPower;
    _lastBatPercent = percent;

    int32_t x = display.width() - BAT_MARGIN - BAT_NUB - BAT_WIDTH;
    int32_t y = TITLE_Y + 2;

    // Anzeigebereich löschen (Symbol + Text links davon)
    display.fillRect(x - 46, y - 2, 46 + BAT_WIDTH + BAT_NUB + BAT_MARGIN, BAT_HEIGHT + 4,
                     TFT_BLACK);

    // Gehäuse mit Kontakt-Nase
    display.drawRect(x, y, BAT_WIDTH, BAT_HEIGHT, TFT_WHITE);

    display.fillRect(x + BAT_WIDTH, y + (BAT_HEIGHT - 6) / 2, BAT_NUB, 6, TFT_WHITE);

    display.setTextSize(1);

    display.setTextDatum(textdatum_t::middle_right);

    if (usbPower)
    {
        // USB-Versorgung: Symbol gefüllt, Beschriftung "USB"
        display.fillRect(x + 2, y + 2, BAT_WIDTH - 4, BAT_HEIGHT - 4, TFT_CYAN);

        display.setTextColor(TFT_CYAN);

        display.drawString("USB", x - 4, y + BAT_HEIGHT / 2);
    }
    else
    {
        // Füllstand: grün / gelb / rot
        uint16_t color = percent > 50 ? TFT_GREEN : (percent > 20 ? TFT_YELLOW : TFT_RED);

        int32_t fill = (BAT_WIDTH - 4) * percent / 100;

        if (fill > 0)
        {
            display.fillRect(x + 2, y + 2, fill, BAT_HEIGHT - 4, color);
        }

        display.setTextColor(TFT_WHITE);

        char label[8];

        snprintf(label, sizeof(label), "%d%%", percent);

        display.drawString(label, x - 4, y + BAT_HEIGHT / 2);
    }
}

void DisplayController::showStatus(bool ble, bool wifi, bool rtp, bool portal)
{
    if (_statusDrawn && ble == _lastBle && wifi == _lastWifi && rtp == _lastRtp &&
        portal == _lastPortal)
    {
        return;
    }

    _lastBle    = ble;
    _lastWifi   = wifi;
    _lastRtp    = rtp;
    _lastPortal = portal;

    _statusDrawn = true;

    clearStatusLine();

    display.setTextSize(1.5);

    display.setTextDatum(textdatum_t::top_left);

    display.setTextColor(ble ? TFT_CYAN : TFT_DARKGREY);
    display.drawString("BLE", 10, STATUS_Y);

    display.setTextColor(wifi ? TFT_YELLOW : TFT_DARKGREY);
    display.drawString("WLAN", 50, STATUS_Y);

    display.setTextColor(rtp ? TFT_GREEN : TFT_DARKGREY);
    display.drawString("RTP", 100, STATUS_Y);

    // Nur sichtbar, solange das WLAN-Setup-Portal offen ist
    if (portal)
    {
        display.setTextColor(TFT_MAGENTA);
        display.drawString("SETUP", 140, STATUS_Y);
    }
}
