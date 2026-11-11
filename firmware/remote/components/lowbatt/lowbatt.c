#include "lowbatt.h"
#include "board.h"
#include "ui.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "esp_log.h"

static const char *TAG = "lowbatt";

#define LOW_MV       CONFIG_REMOTE_LOW_BATT_MV
#define RESUME_MV    (CONFIG_REMOTE_LOW_BATT_MV + CONFIG_REMOTE_LOW_BATT_HYST_MV)
#define CRITICAL_MV  (CONFIG_REMOTE_LOW_BATT_MV - 150)  // below this, skip the final refresh
#define LOW_READINGS 5      // consecutive low polls (~10 s) before shutting down; rejects load sag

// Survives deep sleep; cleared on a power-on reset.
static RTC_DATA_ATTR bool s_slept_low;

int lowbatt_threshold_mv(void) { return LOW_MV; }
int lowbatt_resume_mv(void) { return RESUME_MV; }

bool lowbatt_boot_check(void)
{
    bool woke = esp_reset_reason() == ESP_RST_DEEPSLEEP && s_slept_low;
    s_slept_low = false;
    bool usb = power_usb_present();
    int mv = power_vbat_mv();
    if (usb || mv < 0) return false;
    if (woke) {
        if (mv >= RESUME_MV) { ESP_LOGI(TAG, "battery %d mV: resuming", mv); return false; }
        // The screen still shows "Battery low"; don't spend energy on it.
        ESP_LOGW(TAG, "woke at %d mV (< %d mV resume): back to sleep", mv, RESUME_MV);
        s_slept_low = true;
        power_deep_sleep();
    }
    if (mv < LOW_MV) { ESP_LOGW(TAG, "battery %d mV at power-on", mv); return true; }
    return false;
}

void lowbatt_show_screen(void)
{
    ui_lock();
    pair_screen_show("Sleeping", NULL, "Battery low", "Plug in USB to charge. It wakes up by itself.", NULL);
    ui_unlock();
}

void lowbatt_shutdown(const char *why)
{
    ESP_LOGW(TAG, "shutting down: %s", why);
    esp_wifi_stop();                        // biggest load first; harmless if Wi-Fi never started
    int mv = power_vbat_mv();
    bool show = mv < 0 || mv >= CRITICAL_MV;
    if (show) lowbatt_show_screen();
    else ESP_LOGW(TAG, "%d mV is critical: no final refresh", mv);
    ui_halt(show, 6000);
    s_slept_low = true;
    power_deep_sleep();
}

void lowbatt_poll(void)
{
    static int low;
    if (power_usb_present()) { low = 0; return; }
    int mv = power_vbat_mv();
    if (mv < 0 || mv >= LOW_MV) { low = 0; return; }
    low++;
    ESP_LOGW(TAG, "battery %d mV < %d mV (%d/%d)", mv, LOW_MV, low, LOW_READINGS);
    if (low >= LOW_READINGS || (mv < CRITICAL_MV && low >= 2)) lowbatt_shutdown("battery low");
}
