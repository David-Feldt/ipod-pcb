// Charger mode control, USB presence and battery sensing.
// Requirements: FIRMWARE-NOTES.md "Charger control and USB power" and
// "Low-battery detection and sleep".
#include "board.h"
#include "pins.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "esp_rom_sys.h"

static const char *TAG = "power";

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali[10];        // indexed by ADC1 channel
static charger_mode_t s_mode = CHG_USB100;

static int gpio_to_adc1_channel(int gpio)
{
    // ESP32-S3: ADC1_CH0..CH9 = GPIO1..GPIO10
    return (gpio >= 1 && gpio <= 10) ? gpio - 1 : -1;
}

static esp_err_t adc_setup_channel(int gpio)
{
    int ch = gpio_to_adc1_channel(gpio);
    if (ch < 0) return ESP_ERR_INVALID_ARG;
    adc_oneshot_chan_cfg_t cc = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    esp_err_t err = adc_oneshot_config_channel(s_adc, ch, &cc);
    if (err != ESP_OK) return err;
    adc_cali_curve_fitting_config_t cal = {
        .unit_id = ADC_UNIT_1, .chan = ch, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_curve_fitting(&cal, &s_cali[ch]);
    if (err != ESP_OK) ESP_LOGW(TAG, "no ADC calibration for GPIO%d (%s)", gpio, esp_err_to_name(err));
    return ESP_OK;
}

int board_adc_read_mv(int gpio, int samples)
{
    int ch = gpio_to_adc1_channel(gpio);
    if (ch < 0 || !s_adc) return -1;
    if (samples < 1) samples = 1;
    int64_t sum = 0;
    for (int i = 0; i < samples; i++) {
        int raw = 0, mv = 0;
        if (adc_oneshot_read(s_adc, ch, &raw) != ESP_OK) return -1;
        if (s_cali[ch] && adc_cali_raw_to_voltage(s_cali[ch], raw, &mv) == ESP_OK) sum += mv;
        else sum += raw * 3100 / 4095;      // uncalibrated fallback
    }
    return (int)(sum / samples);
}

void power_set_charger_mode(charger_mode_t m)
{
    // Normal transitions are USB100 <-> USB500, which only ever toggle EN1
    // with EN2 held LOW, so no intermediate state selects ILIM. Clearing EN2
    // first keeps that true when leaving ILIM/SUSPEND as well.
    int en1 = (m == CHG_USB500 || m == CHG_SUSPEND);
    int en2 = (m == CHG_ILIM || m == CHG_SUSPEND);
    if (en2 == 0) { gpio_set_level(PIN_CHG_EN2, 0); gpio_set_level(PIN_CHG_EN1, en1); }
    else          { gpio_set_level(PIN_CHG_EN1, en1); gpio_set_level(PIN_CHG_EN2, en2); }
    if (m != s_mode) ESP_LOGI(TAG, "charger %s -> %s", power_charger_mode_name(s_mode), power_charger_mode_name(m));
    s_mode = m;
}

charger_mode_t power_charger_mode(void) { return s_mode; }

const char *power_charger_mode_name(charger_mode_t m)
{
    static const char *n[] = { "USB100", "USB500", "ILIM", "SUSPEND" };
    return (m <= CHG_SUSPEND) ? n[m] : "?";
}

bool power_usb_present(void) { return gpio_get_level(PIN_USB_PRESENT_N) == 0; }

void power_apply_usb_policy(void)
{
    // Accepted prototype policy (FIRMWARE-NOTES.md, 2026-10-20): select USB500
    // early when USB is present, assuming a >=500 mA source. Not negotiated
    // permission. Without USB, fall back to the USB100 default.
    power_set_charger_mode(power_usb_present() ? CHG_USB500 : CHG_USB100);
}

int power_vbat_mv(void)
{
    int mv = board_adc_read_mv(PIN_VBAT_SENSE, 16);
    return mv < 0 ? -1 : mv * 2;
}

int power_battery_pct(int vbat_mv)
{
    // Rough resting-voltage curve for a 1S LiPo. Display hint only; it is
    // not used for shutdown decisions (those use CONFIG_REMOTE_LOW_BATT_MV).
    static const struct { int mv, pct; } t[] = {
        {4150, 100}, {4050, 90}, {3970, 80}, {3900, 70}, {3840, 60}, {3790, 50},
        {3750, 40}, {3710, 30}, {3670, 20}, {3610, 10}, {3500, 5}, {3300, 0},
    };
    if (vbat_mv >= t[0].mv) return 100;
    for (size_t i = 1; i < sizeof t / sizeof t[0]; i++) {
        if (vbat_mv >= t[i].mv)
            return t[i].pct + (vbat_mv - t[i].mv) * (t[i - 1].pct - t[i].pct) / (t[i - 1].mv - t[i].mv);
    }
    return 0;
}

esp_err_t power_init(void)
{
    // Drive EN1/EN2 LOW before enabling the outputs so initialization never
    // selects a higher-current mode (matches the external pull-downs).
    gpio_set_level(PIN_CHG_EN1, 0);
    gpio_set_level(PIN_CHG_EN2, 0);
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_CHG_EN1) | (1ULL << PIN_CHG_EN2) | (1ULL << PIN_VOL_EN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_set_level(PIN_VOL_EN, 0);
    esp_err_t err = gpio_config(&out);
    if (err != ESP_OK) return err;
    gpio_set_level(PIN_CHG_EN1, 0);
    gpio_set_level(PIN_CHG_EN2, 0);
    gpio_set_level(PIN_VOL_EN, 0);
    s_mode = CHG_USB100;
    // Release the deep-sleep hold (power_deep_sleep) only now that the
    // outputs already drive the same LOW levels.
    gpio_hold_dis(PIN_CHG_EN1);
    gpio_hold_dis(PIN_CHG_EN2);
    gpio_hold_dis(PIN_VOL_EN);

    gpio_config_t usb = {   // external pull-up R8; disable internal pulls
        .pin_bit_mask = 1ULL << PIN_USB_PRESENT_N,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    if ((err = gpio_config(&usb)) != ESP_OK) return err;

    adc_oneshot_unit_init_cfg_t uc = { .unit_id = ADC_UNIT_1 };
    if ((err = adc_oneshot_new_unit(&uc, &s_adc)) != ESP_OK) return err;
    if ((err = adc_setup_channel(PIN_VBAT_SENSE)) != ESP_OK) return err;
    if ((err = adc_setup_channel(PIN_VOL_SENSE)) != ESP_OK) return err;
    return ESP_OK;
}

void power_deep_sleep(void)
{
    // Wheel pot off and charger in its USB100 default, both held through
    // sleep (GPIO5/6/7 are RTC pads, so gpio_hold_en works in deep sleep).
    gpio_set_level(PIN_VOL_EN, 0);
    power_set_charger_mode(CHG_USB100);
    gpio_hold_en(PIN_VOL_EN);
    gpio_hold_en(PIN_CHG_EN1);
    gpio_hold_en(PIN_CHG_EN2);

    // Wake on any front button (BOOT/GPIO0 is a strapping pin: not used) or on
    // USB being plugged in. A button already held (or stuck) is left out, so
    // it cannot wake the board straight back up; likewise USB when present.
    static const int btns[] = { PIN_BTN_MENU, PIN_BTN_PLAY, PIN_BTN_PREV, PIN_BTN_NEXT, PIN_BTN_SEL };
    uint64_t bmask = 0;
    for (size_t i = 0; i < sizeof btns / sizeof btns[0]; i++) bmask |= 1ULL << btns[i];
    gpio_config_t in = { .pin_bit_mask = bmask, .mode = GPIO_MODE_INPUT,
                         .pull_up_en = GPIO_PULLUP_ENABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE };
    gpio_config(&in);
    esp_rom_delay_us(200);
    uint64_t wake = 0;
    for (size_t i = 0; i < sizeof btns / sizeof btns[0]; i++) {
        if (gpio_get_level(btns[i]) == 0) { ESP_LOGW(TAG, "GPIO%d held: not a wake source", btns[i]); continue; }
        rtc_gpio_pullup_en(btns[i]);            // pull-ups must survive deep sleep
        rtc_gpio_pulldown_dis(btns[i]);
        wake |= 1ULL << btns[i];
    }
    if (!power_usb_present()) wake |= 1ULL << PIN_USB_PRESENT_N;   // external pull-up R8
    if (wake) esp_sleep_enable_ext1_wakeup_io(wake, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);   // keeps the RTC pull-ups
    ESP_LOGW(TAG, "deep sleep (wake mask 0x%llx)", (unsigned long long)wake);
    esp_deep_sleep_start();
}
