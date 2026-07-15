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

// LiPo-Spannungsgrenzen für die Prozentrechnung
constexpr uint32_t BAT_EMPTY_MV = 3300;
constexpr uint32_t BAT_FULL_MV  = 4200;

// Darüber liegt USB-Versorgung an (kein sinnvoller Batteriewert)
constexpr uint32_t USB_POWER_MV = 4400;

// ------------------------------------------------
// Helfer
// ------------------------------------------------

static const char* noteName(uint8_t note)
{
    static const char* names[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                                    "F#", "G",  "G#", "A",  "A#", "B"};

    static char buf[8];

    snprintf(buf, sizeof(buf), "%s%d", names[note % 12], note / 12 - 1);

    return buf;
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

    display.fillScreen(TFT_BLACK);

    display.setTextSize(2);

    display.setTextDatum(textdatum_t::top_left);

    display.setTextColor(TFT_WHITE);

    display.drawString("Gemuese-MIDI-Device", 10, TITLE_Y);
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

    for (uint8_t i = 0; i < NUM_SENSORS; i++)
    {
        drawPad(i, false);
    }
}

void DisplayController::drawPad(uint8_t index, bool pressed)
{
    int32_t padWidth = (display.width() - (NUM_SENSORS + 1) * PAD_GAP) / NUM_SENSORS;

    int32_t x = PAD_GAP + index * (padWidth + PAD_GAP);

    display.fillRoundRect(x, PAD_Y, padWidth, PAD_HEIGHT, 6, pressed ? TFT_WHITE : TFT_DARKGREY);

    display.setTextSize(2);

    display.setTextDatum(textdatum_t::middle_center);

    display.setTextColor(pressed ? TFT_BLACK : TFT_WHITE);

    display.drawString(noteName(midiNotes[index]), x + padWidth / 2, PAD_Y + PAD_HEIGHT / 2);
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
    display.fillRect(x - 46, y - 2, 46 + BAT_WIDTH + BAT_NUB + BAT_MARGIN, BAT_HEIGHT + 4, TFT_BLACK);

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

        display.drawString(String(percent) + "%", x - 4, y + BAT_HEIGHT / 2);
    }
}

void DisplayController::showStatus(bool ble, bool wifi, bool rtp)
{
    if (_statusDrawn && ble == _lastBle && wifi == _lastWifi && rtp == _lastRtp)
    {
        return;
    }

    _lastBle  = ble;
    _lastWifi = wifi;
    _lastRtp  = rtp;

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
}
