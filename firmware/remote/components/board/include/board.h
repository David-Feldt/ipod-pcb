#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// ---------------------------------------------------------------- power
typedef enum {
    CHG_USB100 = 0,   // EN1 LOW,  EN2 LOW  (hardware default)
    CHG_USB500,       // EN1 HIGH, EN2 LOW
    CHG_ILIM,         // EN1 LOW,  EN2 HIGH (resistor limit; unused in normal policy)
    CHG_SUSPEND,      // EN1 HIGH, EN2 HIGH
} charger_mode_t;

esp_err_t power_init(void);                 // drives EN1/EN2 LOW first, sets up ADC
bool power_usb_present(void);
void power_set_charger_mode(charger_mode_t m);
charger_mode_t power_charger_mode(void);
const char *power_charger_mode_name(charger_mode_t m);
void power_apply_usb_policy(void);          // USB present -> USB500, else USB100
int power_vbat_mv(void);                    // calibrated, x2 divider, averaged
int power_battery_pct(int vbat_mv);         // rough open-circuit estimate, 0..100
// Hold VOL_EN/EN1/EN2 LOW, arm wake on the front buttons and USB insertion,
// and enter deep sleep. Does not return (waking is a reset).
void power_deep_sleep(void) __attribute__((noreturn));

// ---------------------------------------------------------------- mode
typedef enum { MODE_WIFI, MODE_BLUETOOTH } board_mode_t;
board_mode_t board_boot_mode(void);         // fixed for this boot
bool board_mode_manual(void);               // Bluetooth was chosen by the user, not a fallback
void board_restart_into(board_mode_t m, bool manual) __attribute__((noreturn));

// Shared ADC1 helper (used by power and wheel). Returns calibrated mV.
int board_adc_read_mv(int gpio, int samples);

// ---------------------------------------------------------------- input
typedef enum {
    BTN_MENU = 0, BTN_PLAY, BTN_PREV, BTN_NEXT, BTN_SEL, BTN_BOOT, BTN_COUNT
} button_id_t;

typedef enum {
    INPUT_BTN_PRESS,      // debounced press edge
    INPUT_BTN_RELEASE,    // release (short press if long_fired == false)
    INPUT_BTN_LONG,       // held >= INPUT_LONG_PRESS_MS
    INPUT_WHEEL,          // deliberate wheel movement, value = 0..100 %
} input_event_type_t;

typedef struct {
    input_event_type_t type;
    button_id_t button;
    bool long_fired;      // on RELEASE: a LONG event already fired for this press
    int value;            // wheel percent
} input_event_t;

#define INPUT_DEBOUNCE_MS    25
#define INPUT_LONG_PRESS_MS  800

esp_err_t input_start(QueueHandle_t events);
const char *input_button_name(button_id_t b);
bool input_button_down(button_id_t b);      // raw debounced state
bool input_button_held_at_boot(button_id_t b);

// Wheel
typedef struct { int min_mv, max_mv; } wheel_cal_t;
int  wheel_read_mv(void);                   // VOL_EN high, ~1 ms, sample, VOL_EN low
int  wheel_percent(void);                   // last filtered position 0..100
void wheel_set_calibration(wheel_cal_t c);
wheel_cal_t wheel_calibration(void);
