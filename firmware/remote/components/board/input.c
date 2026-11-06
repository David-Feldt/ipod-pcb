// Buttons (active LOW, pull-ups, 25 ms debounce) and the absolute volume wheel.
// Wheel read sequence per FIRMWARE-NOTES.md: VOL_EN HIGH, wait ~1 ms, sample
// GPIO4 at max attenuation, VOL_EN LOW. Only deliberate movement is published.
#include <stdlib.h>
#include "board.h"
#include "pins.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/task.h"

static const char *TAG = "input";

static const int s_btn_pin[BTN_COUNT] = {
    PIN_BTN_MENU, PIN_BTN_PLAY, PIN_BTN_PREV, PIN_BTN_NEXT, PIN_BTN_SEL, PIN_BTN_BOOT,
};
static const char *s_btn_name[BTN_COUNT] = { "MENU", "PLAY", "PREV", "NEXT", "SEL", "BOOT" };

typedef struct {
    bool down;            // debounced
    bool raw;
    int64_t raw_since_ms;
    int64_t down_since_ms;
    bool long_fired;
} btn_state_t;

static btn_state_t s_btn[BTN_COUNT];
static bool s_held_at_boot[BTN_COUNT];
static QueueHandle_t s_events;

// Wheel
#define WHEEL_PERIOD_MS     40
#define WHEEL_START_PCT     3     // movement from rest needed before publishing
#define WHEEL_SETTLE_MS     1500  // no change for this long -> back to rest
static wheel_cal_t s_cal = { .min_mv = 20, .max_mv = 2715 };  // bench: 0..2735 mV
static volatile int s_wheel_pct = -1;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

const char *input_button_name(button_id_t b) { return b < BTN_COUNT ? s_btn_name[b] : "?"; }
bool input_button_down(button_id_t b) { return b < BTN_COUNT && s_btn[b].down; }
bool input_button_held_at_boot(button_id_t b) { return b < BTN_COUNT && s_held_at_boot[b]; }

void wheel_set_calibration(wheel_cal_t c)
{
    if (c.max_mv - c.min_mv >= 500) s_cal = c;
    else ESP_LOGW(TAG, "rejecting wheel calibration %d..%d mV", c.min_mv, c.max_mv);
}
wheel_cal_t wheel_calibration(void) { return s_cal; }
int wheel_percent(void) { return s_wheel_pct; }

int wheel_read_mv(void)
{
    gpio_set_level(PIN_VOL_EN, 1);
    esp_rom_delay_us(1000);
    int mv = board_adc_read_mv(PIN_VOL_SENSE, 8);
    gpio_set_level(PIN_VOL_EN, 0);
    return mv;
}

static int wheel_mv_to_pct(int mv)
{
    int span = s_cal.max_mv - s_cal.min_mv;
    int p = (mv - s_cal.min_mv) * 100 / span;   // calibration guarantees span >= 500
    return p < 0 ? 0 : p > 100 ? 100 : p;
}

static void emit(input_event_t ev)
{
    if (s_events && xQueueSend(s_events, &ev, 0) != pdTRUE) ESP_LOGW(TAG, "event queue full");
}

static void poll_buttons(int64_t t)
{
    for (int i = 0; i < BTN_COUNT; i++) {
        btn_state_t *b = &s_btn[i];
        bool raw = gpio_get_level(s_btn_pin[i]) == 0;
        if (raw != b->raw) { b->raw = raw; b->raw_since_ms = t; }
        if (raw != b->down && t - b->raw_since_ms >= INPUT_DEBOUNCE_MS) {
            b->down = raw;
            if (raw) {
                b->down_since_ms = t;
                b->long_fired = false;
                emit((input_event_t){ .type = INPUT_BTN_PRESS, .button = i });
            } else {
                emit((input_event_t){ .type = INPUT_BTN_RELEASE, .button = i, .long_fired = b->long_fired });
            }
        }
        if (b->down && !b->long_fired && t - b->down_since_ms >= INPUT_LONG_PRESS_MS) {
            b->long_fired = true;
            emit((input_event_t){ .type = INPUT_BTN_LONG, .button = i });
        }
    }
}

static void poll_wheel(int64_t t)
{
    static int filt = -1;          // filtered percent x16
    static int rest_pct = -1;      // position when wheel last came to rest
    static int last_sent = -1;
    static bool moving = false;
    static int64_t last_change_ms = 0;

    int mv = wheel_read_mv();
    if (mv < 0) return;
    int p16 = wheel_mv_to_pct(mv) * 16;
    filt = (filt < 0) ? p16 : filt + (p16 - filt) / 4;      // light EMA
    int pct = (filt + 8) / 16;
    s_wheel_pct = pct;

    if (rest_pct < 0) { rest_pct = pct; last_sent = pct; return; }   // boot: never publish

    if (!moving) {
        if (abs(pct - rest_pct) >= WHEEL_START_PCT) {
            moving = true;
            last_change_ms = t;
            last_sent = pct;
            emit((input_event_t){ .type = INPUT_WHEEL, .value = pct });
        }
    } else {
        if (pct != last_sent) {
            last_sent = pct;
            last_change_ms = t;
            emit((input_event_t){ .type = INPUT_WHEEL, .value = pct });
        } else if (t - last_change_ms >= WHEEL_SETTLE_MS) {
            moving = false;
            rest_pct = pct;
        }
    }
}

static void input_task(void *arg)
{
    int64_t next_wheel = 0;
    for (;;) {
        int64_t t = now_ms();
        poll_buttons(t);
        if (t >= next_wheel) { poll_wheel(t); next_wheel = t + WHEEL_PERIOD_MS; }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

esp_err_t input_start(QueueHandle_t events)
{
    s_events = events;
    uint64_t mask = 0;
    for (int i = 0; i < BTN_COUNT; i++) mask |= 1ULL << s_btn_pin[i];
    gpio_config_t cfg = {
        .pin_bit_mask = mask, .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) return err;
    esp_rom_delay_us(200);    // let pull-ups settle

    int64_t t = now_ms();
    for (int i = 0; i < BTN_COUNT; i++) {
        bool down = gpio_get_level(s_btn_pin[i]) == 0;
        s_btn[i] = (btn_state_t){ .down = down, .raw = down, .raw_since_ms = t, .down_since_ms = t,
                                  .long_fired = true /* no LONG for boot-held buttons */ };
        s_held_at_boot[i] = down;
        if (down) ESP_LOGW(TAG, "%s (GPIO%d) LOW at boot", s_btn_name[i], s_btn_pin[i]);
    }
    return xTaskCreate(input_task, "input", 3072, NULL, 6, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
