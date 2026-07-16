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

// Obere Leiste: Titel links, Status-Icons und Batterie rechts
constexpr int32_t TITLE_Y = 10;

constexpr int32_t ICON_Y    = 8;  // Oberkante der Status-Icons
constexpr int32_t ICON_SLOT = 22; // Breite je Icon-Platz

// Die Icon-Leiste ist horizontal zentriert — ihr linker Rand wird in
// showStatus() aus der Displaybreite berechnet.

constexpr int32_t BAT_Y = 8; // Oberkante des Batterie-Symbols

// Pads: feste Höhe, von der Displayunterkante aus verankert.
// PAD_Y wird in begin() aus der Displayhöhe berechnet.
constexpr int32_t PAD_HEIGHT        = 100;
constexpr int32_t PAD_MARGIN_BOTTOM = 4;
constexpr int32_t PAD_GAP           = 4;

// Eckenradius der Tasten (nur unten gerundet, wie beim Klavier)
constexpr int32_t PAD_CORNER_RADIUS = 4;

// Deko-Obertasten ("schwarze Tasten"): Höhe relativ zur weißen Taste
// und Breite in px. Rein optisch — gespielt werden die sieben
// weißen Tasten.
constexpr int32_t BLACK_KEY_HEIGHT = (PAD_HEIGHT * 55) / 100;
constexpr int32_t BLACK_KEY_WIDTH  = 14;

// Nach welchen weißen Tasten eine Obertaste sitzt (C-Dur-Oktave:
// nach C, D, F, G, A — nicht nach E und H)
constexpr bool BLACK_KEY_AFTER[NUM_SENSORS] = {true, true, false, true, true, true, false};

static int32_t PAD_Y = 0;

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

// ------------------------------------------------
// Status-Icons (je 14x14 px, gezeichnet aus Primitiven)
// ------------------------------------------------

// Bluetooth-Rune
static void drawBleIcon(int32_t x, int32_t y, uint16_t color)
{
    int32_t cx = x + 7;

    display.drawLine(cx, y, cx, y + 13, color);
    display.drawLine(cx, y, x + 11, y + 4, color);
    display.drawLine(x + 11, y + 4, x + 3, y + 10, color);
    display.drawLine(cx, y + 13, x + 11, y + 9, color);
    display.drawLine(x + 11, y + 9, x + 3, y + 3, color);
}

// WLAN-Fächer: Punkt + drei Bögen nach oben
static void drawWifiIcon(int32_t x, int32_t y, uint16_t color)
{
    int32_t cx = x + 7;
    int32_t cy = y + 11;

    display.fillCircle(cx, cy, 2, color);
    display.fillArc(cx, cy, 4, 5, 225, 315, color);
    display.fillArc(cx, cy, 7, 8, 225, 315, color);
    display.fillArc(cx, cy, 10, 11, 225, 315, color);
}

// Achtelnote (Netzwerk-MIDI bereit)
static void drawRtpIcon(int32_t x, int32_t y, uint16_t color)
{
    display.fillCircle(x + 5, y + 10, 3, color);
    display.drawFastVLine(x + 8, y + 1, 12, color);
    display.drawLine(x + 8, y + 2, x + 12, y + 3, color);
}

// Zahnrad (Setup-Portal aktiv): offener Ring mit acht Zähnen —
// vier orthogonal, vier diagonal
static void drawSetupIcon(int32_t x, int32_t y, uint16_t color)
{
    int32_t cx = x + 7;
    int32_t cy = y + 7;

    // Ring (Loch in der Mitte bleibt frei)
    display.fillArc(cx, cy, 3, 5, 0, 360, color);

    // Zähne orthogonal (oben, unten, links, rechts)
    display.fillRect(cx - 1, cy - 7, 2, 3, color);
    display.fillRect(cx - 1, cy + 5, 2, 3, color);
    display.fillRect(cx - 7, cy - 1, 3, 2, color);
    display.fillRect(cx + 5, cy - 1, 3, 2, color);

    // Zähne diagonal
    display.fillRect(cx - 5, cy - 5, 2, 2, color);
    display.fillRect(cx + 4, cy - 5, 2, 2, color);
    display.fillRect(cx - 5, cy + 4, 2, 2, color);
    display.fillRect(cx + 4, cy + 4, 2, 2, color);
}

// Lautsprecher (Standalone-Betrieb): Korpus, Membran, Schallbögen
static void drawSpeakerIcon(int32_t x, int32_t y, uint16_t color)
{
    display.fillRect(x + 1, y + 5, 3, 5, color);
    display.fillTriangle(x + 3, y + 7, x + 8, y + 2, x + 8, y + 12, color);
    display.fillArc(x + 8, y + 7, 2, 3, 300, 60, color);
    display.fillArc(x + 8, y + 7, 5, 6, 300, 60, color);
}

// Breite einer weißen Taste (aus der Displaybreite)
static int32_t keyWidth()
{
    return (display.width() - (NUM_SENSORS + 1) * PAD_GAP) / NUM_SENSORS;
}

// Zeichnet die Deko-Obertasten, die an die weiße Taste `index`
// angrenzen — nach jedem Neuzeichnen der Taste aufrufen, damit die
// Obertasten optisch "vorne" bleiben wie beim echten Klavier.
static void drawBlackKeysAround(uint8_t index)
{
    int32_t w = keyWidth();

    for (int8_t k = index - 1; k <= index; k++)
    {
        if (k < 0 || k >= NUM_SENSORS - 1 || !BLACK_KEY_AFTER[k])
        {
            continue;
        }

        // Obertaste sitzt mittig über der Fuge zwischen Taste k und k+1
        int32_t fuge = PAD_GAP + (k + 1) * (w + PAD_GAP) - PAD_GAP / 2;

        display.fillRoundRect(fuge - BLACK_KEY_WIDTH / 2, PAD_Y, BLACK_KEY_WIDTH, BLACK_KEY_HEIGHT,
                              2, TFT_BLACK);
    }
}

// Zeichnet den Notennamen unten ins Pad. `onFill` = Text liegt auf der
// hellen Velocity-Füllung (dann schwarz für den Kontrast).
static void drawPadLabel(uint8_t index, int32_t x, int32_t padWidth)
{
    char label[8];

    noteName(midiNotes[index], label, sizeof(label));

    display.setFont(&fonts::DejaVu18);

    display.setTextSize(1);

    display.setTextDatum(textdatum_t::middle_center);

    // Auf der weißen Taste (und auf der farbigen Füllung) ist Schwarz
    // immer lesbar
    display.setTextColor(TFT_BLACK);

    display.drawString(label, x + padWidth / 2, PAD_Y + PAD_HEIGHT - PAD_LABEL_CENTER);

    display.unloadFont();
}

// Zeichnet die Peak-Markerlinie auf Höhe `pos` (px ab Pad-Unterkante)
static void drawPeakMarker(int32_t x, int32_t padWidth, int32_t pos, uint8_t velocity)
{
    display.fillRect(x + 2, PAD_Y + 2 + (PAD_INNER - pos), padWidth - 4, PEAK_MARKER_H,
                     velocityColor(velocity));
}

static void clearPadArea()
{
    display.fillRect(0, PAD_Y, display.width(), PAD_HEIGHT, TFT_BLACK);
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

    display.setRotation(DISPLAY_ROTATION);

    display.setBrightness(DISPLAY_BRIGHNESS);

    display.fillScreen(TFT_BLACK);

    // Pads an der Unterkante verankern
    PAD_Y = display.height() - PAD_MARGIN_BOTTOM - PAD_HEIGHT;

    display.setFont(&fonts::DejaVu12);

    display.setTextSize(1);

    display.setTextDatum(textdatum_t::top_left);

    display.setTextColor(TFT_WHITE);

    // Kurzform — der volle Name würde in die zentrierte Icon-Leiste ragen
    display.drawString(MIDI_DEVICE_NAME, 10, TITLE_Y);

    display.unloadFont();
}

void DisplayController::showCalibrating()
{
    clearPadArea();

    display.setTextSize(1);

    display.setFont(&fonts::DejaVu18);

    display.setTextDatum(textdatum_t::middle_center);

    display.setTextColor(TFT_WHITE);

    display.setTextDatum(textdatum_t::middle_center);

    display.drawString("Calibrating", display.width() / 2, display.height() / 2);

    display.unloadFont();
}

void DisplayController::showPads()
{
    clearPadArea();

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
    int32_t padWidth = keyWidth();

    int32_t x = PAD_GAP + index * (padWidth + PAD_GAP);

    _padPressed[index] = pressed;

    // Weiße Taste: unten gerundet, oben eckig (Rechteck über die
    // oberen Ecken legt sie frei — wie beim Klavier)
    display.fillRoundRect(x, PAD_Y, padWidth, PAD_HEIGHT, PAD_CORNER_RADIUS, TFT_WHITE);
    display.fillRect(x, PAD_Y, padWidth, PAD_CORNER_RADIUS, TFT_WHITE);

    if (pressed)
    {
        // Füllstand von unten, Höhe und Farbe nach Velocity (VU-Stil).
        int32_t fill = PAD_INNER * velocity / 127;

        if (fill > 0)
        {
            int32_t yTop = PAD_Y + 2 + (PAD_INNER - fill);

            // Unten gerundet wie die Taste selbst: die Füllung liegt
            // 2 px innerhalb, der passende (konzentrische) Radius ist
            // also um 2 kleiner — bei sehr kleinem Füllstand begrenzt.
            int32_t r = PAD_CORNER_RADIUS - 2;

            if (r > fill / 2)
            {
                r = fill / 2;
            }

            if (r < 0)
            {
                r = 0;
            }

            display.fillRoundRect(x + 2, yTop, padWidth - 4, fill, r, velocityColor(velocity));

            // Obere Ecken der Füllung wieder eckig machen — nur die
            // Unterkante folgt der Tastenrundung
            display.fillRect(x + 2, yTop, padWidth - 4, r, velocityColor(velocity));
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

    // Notenname unten auf der Taste
    drawPadLabel(index, x, padWidth);

    // Angrenzende Obertasten wieder "nach vorne" holen
    drawBlackKeysAround(index);
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

    int32_t padWidth = keyWidth();

    for (uint8_t i = 0; i < NUM_SENSORS; i++)
    {
        // Nur losgelassene Pads mit Marker, deren Haltezeit abgelaufen
        // ist (Differenz-Vergleich: robust gegen millis()-Überlauf)
        if (_padPressed[i] || _peakPos[i] <= 0 || static_cast<int32_t>(now - _peakHoldUntil[i]) < 0)
        {
            continue;
        }

        int32_t x = PAD_GAP + i * (padWidth + PAD_GAP);

        // Alten Marker löschen (Tastenfarbe: Weiß)
        display.fillRect(x + 2, PAD_Y + 2 + (PAD_INNER - _peakPos[i]), padWidth - 4, PEAK_MARKER_H,
                         TFT_WHITE);

        int32_t oldPos = _peakPos[i];

        _peakPos[i] -= VELOCITY_PEAK_FALL_PX;

        if (_peakPos[i] >= PAD_CORNER_RADIUS + PEAK_MARKER_H)
        {
            drawPeakMarker(x, padWidth, _peakPos[i], _peakVelocity[i]);
        }
        else
        {
            // Kurz vor der gerundeten Unterkante verschwinden — so
            // malt das Löschen nie in die Eckrundung hinein
            _peakPos[i] = 0;
        }

        // Läuft der Marker durch den Notennamen-Bereich, Text auffrischen
        if (oldPos <= PAD_LABEL_ZONE || _peakPos[i] <= PAD_LABEL_ZONE)
        {
            drawPadLabel(i, x, padWidth);
        }

        // Im Bereich der Obertasten: die Deko wieder nach vorne holen
        if (oldPos >= PAD_INNER - BLACK_KEY_HEIGHT)
        {
            drawBlackKeysAround(i);
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
    int32_t y = BAT_Y;

    // Anzeigebereich löschen (Symbol + Text links davon)
    display.fillRect(x - 46, y - 2, 46 + BAT_WIDTH + BAT_NUB + BAT_MARGIN, BAT_HEIGHT + 4,
                     TFT_BLACK);

    // Gehäuse mit Kontakt-Nase
    display.drawRect(x, y, BAT_WIDTH, BAT_HEIGHT, TFT_WHITE);

    display.fillRect(x + BAT_WIDTH, y + (BAT_HEIGHT - 6) / 2, BAT_NUB, 6, TFT_WHITE);

    display.setFont(&fonts::DejaVu12);

    display.setTextSize(1);

    display.setTextDatum(textdatum_t::middle_right);

    if (usbPower)
    {
        // USB-Versorgung: Symbol gefüllt, Beschriftung "USB"
        display.fillRect(x + 2, y + 2, BAT_WIDTH - 4, BAT_HEIGHT - 4, TFT_GREEN);

        display.setTextColor(TFT_WHITE);

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

    display.unloadFont();
}

void DisplayController::showStatus(bool ble, bool wifi, bool rtp, bool portal, bool speaker)
{
    if (_statusDrawn && ble == _lastBle && wifi == _lastWifi && rtp == _lastRtp &&
        portal == _lastPortal && speaker == _lastSpeaker)
    {
        return;
    }

    _lastBle     = ble;
    _lastWifi    = wifi;
    _lastRtp     = rtp;
    _lastPortal  = portal;
    _lastSpeaker = speaker;

    _statusDrawn = true;

    // Icon-Leiste horizontal zentriert in der oberen Zeile
    int32_t iconsX = (display.width() - 5 * ICON_SLOT) / 2;

    display.fillRect(iconsX - 2, ICON_Y - 2, 5 * ICON_SLOT + 4, 18, TFT_BLACK);

    drawBleIcon(iconsX + 0 * ICON_SLOT, ICON_Y, ble ? TFT_CYAN : TFT_DARKGREY);
    drawWifiIcon(iconsX + 1 * ICON_SLOT, ICON_Y, wifi ? TFT_YELLOW : TFT_DARKGREY);
    drawRtpIcon(iconsX + 2 * ICON_SLOT, ICON_Y, rtp ? TFT_GREEN : TFT_DARKGREY);
    drawSetupIcon(iconsX + 3 * ICON_SLOT, ICON_Y, portal ? TFT_MAGENTA : TFT_DARKGREY);
    drawSpeakerIcon(iconsX + 4 * ICON_SLOT, ICON_Y, speaker ? TFT_ORANGE : TFT_DARKGREY);
}
