#include "EncoderController.h"

#include <driver/pcnt.h>

#include "Config.h"

namespace
{
constexpr pcnt_unit_t UNIT = PCNT_UNIT_0;

// Restimpulse unterhalb einer vollen Raste
int32_t stepRemainder = 0;

// Taster-Entprellung
bool lastSw           = true;
uint32_t lastSwChange = 0;
bool clickPending     = false;
} // namespace

void EncoderController::begin()
{
    if (!ENABLE_ENCODER)
    {
        return;
    }

    pinMode(PIN_ENCODER_A, INPUT_PULLUP);
    pinMode(PIN_ENCODER_B, INPUT_PULLUP);
    pinMode(PIN_ENCODER_SW, INPUT_PULLUP);

    // Volle Quadratur (x4): zwei PCNT-Kanäle, A und B wechselseitig
    // als Puls- und Richtungssignal
    pcnt_config_t cfg = {};

    cfg.unit          = UNIT;
    cfg.counter_h_lim = 32767;
    cfg.counter_l_lim = -32768;

    cfg.channel        = PCNT_CHANNEL_0;
    cfg.pulse_gpio_num = PIN_ENCODER_A;
    cfg.ctrl_gpio_num  = PIN_ENCODER_B;
    cfg.pos_mode       = PCNT_COUNT_DEC;
    cfg.neg_mode       = PCNT_COUNT_INC;
    cfg.lctrl_mode     = PCNT_MODE_KEEP;
    cfg.hctrl_mode     = PCNT_MODE_REVERSE;

    pcnt_unit_config(&cfg);

    cfg.channel        = PCNT_CHANNEL_1;
    cfg.pulse_gpio_num = PIN_ENCODER_B;
    cfg.ctrl_gpio_num  = PIN_ENCODER_A;
    cfg.pos_mode       = PCNT_COUNT_INC;
    cfg.neg_mode       = PCNT_COUNT_DEC;
    cfg.lctrl_mode     = PCNT_MODE_KEEP;
    cfg.hctrl_mode     = PCNT_MODE_REVERSE;

    pcnt_unit_config(&cfg);

    // Glitch-Filter in Hardware: Impulse kürzer als 1023 APB-Takte
    // (~12,8 µs) werden verworfen — entprellt die A/B-Spur komplett
    pcnt_set_filter_value(UNIT, 1023);
    pcnt_filter_enable(UNIT);

    pcnt_counter_clear(UNIT);

    Serial.println("Encoder bereit (PCNT)");
}

void EncoderController::update()
{
    if (!ENABLE_ENCODER)
    {
        return;
    }

    bool sw = digitalRead(PIN_ENCODER_SW) == HIGH;

    uint32_t now = millis();

    if (sw != lastSw && now - lastSwChange > 30)
    {
        lastSwChange = now;
        lastSw       = sw;

        // Fallende Flanke = gedrückt
        if (!sw)
        {
            clickPending = true;
        }
    }
}

int32_t EncoderController::readDetents()
{
    if (!ENABLE_ENCODER)
    {
        return 0;
    }

    int16_t count = 0;

    pcnt_get_counter_value(UNIT, &count);

    // Sofort nullen: der Zähler bleibt so immer weit weg von den
    // Limits, ein Überlauf ist bei Handbedienung ausgeschlossen
    pcnt_counter_clear(UNIT);

    stepRemainder += count;

    int32_t detents = stepRemainder / ENCODER_STEPS_PER_DETENT;

    stepRemainder -= detents * ENCODER_STEPS_PER_DETENT;

    return detents;
}

bool EncoderController::clicked()
{
    bool c = clickPending;

    clickPending = false;

    return c;
}