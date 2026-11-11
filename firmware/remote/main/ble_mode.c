// Bluetooth mode (firmware milestone 8): the board is a BLE media remote for
// the phone when no saved Wi-Fi is reachable. Entered by restart from Wi-Fi
// mode (see app_main.c); every few minutes it scans for the saved network and
// restarts into Wi-Fi mode when it is back in range.
//   SEL play/pause   NEXT / PREV tracks   wheel phone volume (relative steps)
//   MENU menu: use Wi-Fi, forget phone   PLAY hold: full refresh
// Chosen from the Wi-Fi menu, it stays in Bluetooth mode (no Wi-Fi probing).
#include <stdlib.h>
#include "ble_mode.h"
#include "board.h"
#include "btremote.h"
#include "lowbatt.h"
#include "net.h"
#include "ui.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ble_mode";

#define DEVICE_NAME       "E-ink Remote"
#define WIFI_PROBE_MS     (3 * 60 * 1000)
#define MENU_IDLE_MS      20000
// iOS has 16 volume steps; one wheel step per 1/16 of its travel.
#define VOL_STEP_PCT_X100 625

typedef enum { MI_WIFI, MI_FORGET, MI_BACK, MI_COUNT } menu_item_t;
static struct { bool open; int sel; int64_t last_ms; } s_menu;
static int s_vol_ref_x100 = -1;     // wheel position (pct x100) the phone volume is synced to

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void show_battery(void)
{
    ui_lock();
    bt_screen_set_battery(power_battery_pct(power_vbat_mv()), power_usb_present());
    ui_unlock();
}

static void show_status(void)
{
    const char *big, *small;
    switch (btremote_state()) {
    case BTR_READY:
        ui_lock();
        bt_screen_show("Connected", board_mode_manual() ? NULL : "No Wi-Fi in range");
        ui_unlock();
        return;
    case BTR_CONNECTED:
        big = "Pairing...";
        small = "Accept the request on the phone";
        break;
    default:
        if (btremote_bond_count() > 0) {
            big = "Waiting for phone";
            small = "Turn on Bluetooth on the phone";
        } else {
            big = "Pair your phone";
            small = "iPhone: Settings > Bluetooth > " DEVICE_NAME;
        }
        break;
    }
    ui_lock();
    pair_screen_show("Bluetooth", NULL, big, small, board_mode_manual() ? "MENU: options" : "No Wi-Fi in range. MENU: options");
    ui_unlock();
}

static void menu_render(void)
{
    static const char *const items[MI_COUNT] = { [MI_WIFI] = "Use Wi-Fi", [MI_FORGET] = "Forget phone",
                                                  [MI_BACK] = "Back" };
    ui_lock(); menu_screen_show(items, MI_COUNT, s_menu.sel); ui_unlock();
}

static void menu_close(void)
{
    s_menu.open = false;
    show_status();
    ui_request_full_refresh();
}

static void menu_input(button_id_t b)
{
    s_menu.last_ms = now_ms();
    switch (b) {
    case BTN_MENU: s_menu.sel = (s_menu.sel + MI_COUNT - 1) % MI_COUNT; menu_render(); break;
    case BTN_PLAY: s_menu.sel = (s_menu.sel + 1) % MI_COUNT; menu_render(); break;
    case BTN_PREV: menu_close(); break;
    case BTN_SEL:
        if (s_menu.sel == MI_WIFI) board_restart_into(MODE_WIFI, true);
        if (s_menu.sel == MI_FORGET) btremote_forget();
        menu_close();
        break;
    default: break;
    }
}

// Absolute wheel -> relative volume keys: send one step per 1/16 of travel
// moved since the last sync. The phone's own volume is not known here.
static void wheel_to_volume(int pct)
{
    int pos = pct * 100;
    if (s_vol_ref_x100 < 0) { s_vol_ref_x100 = pos; return; }
    int steps = (pos - s_vol_ref_x100) / VOL_STEP_PCT_X100;
    if (!steps) return;
    s_vol_ref_x100 += steps * VOL_STEP_PCT_X100;
    if (btremote_state() != BTR_READY) return;
    ESP_LOGI(TAG, "wheel %d%%: volume %+d", pct, steps);
    for (int i = 0; i < abs(steps); i++) btremote_key(steps > 0 ? BTR_VOL_UP : BTR_VOL_DOWN);
}

static void handle_input(const input_event_t *ev)
{
    if (ev->type == INPUT_WHEEL) { wheel_to_volume(ev->value); return; }
    if (ev->type == INPUT_BTN_LONG && ev->button == BTN_PLAY) { ui_request_full_refresh(); return; }
    if (ev->type != INPUT_BTN_PRESS) return;
    ESP_LOGI(TAG, "%s", input_button_name(ev->button));
    if (s_menu.open) { menu_input(ev->button); return; }
    switch (ev->button) {
    case BTN_MENU:
        s_menu = (typeof(s_menu)){ .open = true, .sel = 0, .last_ms = now_ms() };
        menu_render();
        break;
    case BTN_SEL: btremote_key(BTR_PLAY_PAUSE); break;
    case BTN_NEXT: btremote_key(BTR_NEXT); break;
    case BTN_PREV: btremote_key(BTR_PREV); break;
    default: break;
    }
}

static void ble_mode_task(void *arg)
{
    QueueHandle_t events = arg;
    ESP_LOGI(TAG, "Bluetooth mode");
    esp_err_t err = btremote_start(DEVICE_NAME);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth failed to start (%s): back to Wi-Fi", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(1000));
        board_restart_into(MODE_WIFI, false);
    }
    int w = wheel_percent();
    if (w >= 0) s_vol_ref_x100 = w * 100;
    show_battery();
    show_status();

    uint32_t seen = btremote_version();
    int64_t last_power = now_ms(), last_probe = now_ms(), last_batt = 0;
    for (;;) {
        input_event_t ev;
        if (xQueueReceive(events, &ev, pdMS_TO_TICKS(100)) == pdTRUE) handle_input(&ev);
        int64_t t = now_ms();

        uint32_t v = btremote_version();
        if (v != seen) {
            seen = v;
            if (!s_menu.open) show_status();
        }
        if (s_menu.open && t - s_menu.last_ms > MENU_IDLE_MS) menu_close();

        if (t - last_power >= 2000) {
            last_power = t;
            power_apply_usb_policy();
            lowbatt_poll();                 // may enter deep sleep
            if (t - last_batt >= 60000) {
                last_batt = t;
                btremote_set_battery(power_battery_pct(power_vbat_mv()));
                show_battery();             // partial refresh only if the text changed
            }
        }
        if (!board_mode_manual() && t - last_probe >= WIFI_PROBE_MS) {
            last_probe = t;
            int rssi = 0;
            if (net_probe_saved(&rssi)) {
                ESP_LOGI(TAG, "saved Wi-Fi in range (%d dBm)", rssi);
                board_restart_into(MODE_WIFI, false);
            }
            last_probe = now_ms();
        }
    }
}

void ble_mode_start(QueueHandle_t events)
{
    if (xTaskCreate(ble_mode_task, "ble_mode", 6144, events, 5, NULL) != pdPASS) board_restart_into(MODE_WIFI, false);
}
