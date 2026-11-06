// E-ink Remote application entry point.
// Milestone 1: board bring-up in ESP-IDF (power policy, input events, console).
#include "board.h"
#include "settings.h"
#include "app_console.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "app";

static void load_calibration(void)
{
    int32_t mn, mx;
    if (settings_get_i32(SETTING_WHEEL_MIN_MV, &mn) == ESP_OK &&
        settings_get_i32(SETTING_WHEEL_MAX_MV, &mx) == ESP_OK) {
        wheel_set_calibration((wheel_cal_t){ .min_mv = mn, .max_mv = mx });
    }
}

void app_main(void)
{
    // Charger pins first so EN1/EN2 are driven LOW (USB100) as early as possible.
    ESP_ERROR_CHECK(power_init());
    ESP_ERROR_CHECK(settings_init());
    load_calibration();

    // VBAT divider/filter needs >= 250 ms to settle after power-on.
    vTaskDelay(pdMS_TO_TICKS(250));
    power_apply_usb_policy();
    int vbat = power_vbat_mv();
    ESP_LOGI(TAG, "vbat %d mV, usb %s, charger %s", vbat, power_usb_present() ? "present" : "absent",
             power_charger_mode_name(power_charger_mode()));

    QueueHandle_t events = xQueueCreate(16, sizeof(input_event_t));
    ESP_ERROR_CHECK(input_start(events));
    ESP_ERROR_CHECK(app_console_start());

    TickType_t last_power = xTaskGetTickCount();
    for (;;) {
        input_event_t ev;
        if (xQueueReceive(events, &ev, pdMS_TO_TICKS(100)) == pdTRUE) {
            switch (ev.type) {
            case INPUT_BTN_PRESS:   ESP_LOGI(TAG, "%s press", input_button_name(ev.button)); break;
            case INPUT_BTN_LONG:    ESP_LOGI(TAG, "%s long", input_button_name(ev.button)); break;
            case INPUT_BTN_RELEASE: ESP_LOGD(TAG, "%s release", input_button_name(ev.button)); break;
            case INPUT_WHEEL:       ESP_LOGI(TAG, "wheel %d%%", ev.value); break;
            }
        }
        if (xTaskGetTickCount() - last_power >= pdMS_TO_TICKS(2000)) {
            last_power = xTaskGetTickCount();
            power_apply_usb_policy();      // handles USB plug/unplug
        }
    }
}
