// Commands: status, wifi, token, charger, calib, erase, restart.
// Secrets typed here are stored in NVS; they are never echoed back in full.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "app_console.h"
#include "board.h"
#include "settings.h"
#include "esp_console.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "driver/temperature_sensor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static temperature_sensor_handle_t s_tsens;

static const char *reset_reason_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep-sleep wake";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_USB: return "usb";
    default: return "other";
    }
}

static void print_secret_state(const char *label, const char *key)
{
    printf("%-14s %s\n", label, settings_has(key) ? "set" : "not set");
}

static int cmd_status(int argc, char **argv)
{
    const esp_app_desc_t *app = esp_app_get_description();
    int vbat = power_vbat_mv();
    float temp = 0;
    if (s_tsens) temperature_sensor_get_celsius(s_tsens, &temp);
    printf("firmware       %s %s (%s %s)\n", app->project_name, app->version, app->date, app->time);
    printf("uptime         %lld s, reset: %s\n", esp_timer_get_time() / 1000000, reset_reason_name(esp_reset_reason()));
    printf("usb            %s, charger %s\n", power_usb_present() ? "present" : "absent",
           power_charger_mode_name(power_charger_mode()));
    printf("battery        %d mV (~%d%%)\n", vbat, power_battery_pct(vbat));
    wheel_cal_t c = wheel_calibration();
    printf("wheel          %d mV, %d%% (cal %d..%d mV)\n", wheel_read_mv(), wheel_percent(), c.min_mv, c.max_mv);
    printf("buttons        ");
    for (int b = 0; b < BTN_COUNT; b++) printf("%s=%s ", input_button_name(b), input_button_down(b) ? "DOWN" : "up");
    printf("\n");
    printf("chip temp      %.1f C\n", temp);
    printf("heap           free %u, min free %u, largest %u\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    char ssid[33];
    if (settings_get_str(SETTING_WIFI_SSID, ssid, sizeof ssid) == ESP_OK) printf("%-14s %s\n", "wifi ssid", ssid);
    else printf("%-14s not set\n", "wifi ssid");
    print_secret_state("spotify token", SETTING_REFRESH_TOKEN);
    return 0;
}

static int cmd_wifi(int argc, char **argv)
{
    if (argc == 4 && !strcmp(argv[1], "set")) {
        if (strlen(argv[2]) > 32 || strlen(argv[3]) > 64) { printf("ssid <= 32, password <= 64 chars\n"); return 1; }
        esp_err_t e1 = settings_set_str(SETTING_WIFI_SSID, argv[2]);
        esp_err_t e2 = settings_set_str(SETTING_WIFI_PASS, argv[3]);
        printf("%s\n", (e1 == ESP_OK && e2 == ESP_OK) ? "saved; restart to connect" : "save failed");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "clear")) {
        settings_erase(SETTING_WIFI_SSID);
        settings_erase(SETTING_WIFI_PASS);
        printf("cleared\n");
        return 0;
    }
    printf("usage: wifi set <ssid> <password> | wifi clear  (quote values with spaces)\n");
    return 1;
}

static int cmd_token(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "set")) {
        size_t n = strlen(argv[2]);
        if (n < 20 || n > 512) { printf("that does not look like a refresh token\n"); return 1; }
        printf("%s\n", settings_set_str(SETTING_REFRESH_TOKEN, argv[2]) == ESP_OK ? "saved" : "save failed");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "clear")) {
        settings_erase(SETTING_REFRESH_TOKEN);
        printf("cleared\n");
        return 0;
    }
    printf("usage: token set <spotify_refresh_token> | token clear\n");
    return 1;
}

static int cmd_charger(int argc, char **argv)
{
    if (argc == 2) {
        if (!strcmp(argv[1], "usb100")) power_set_charger_mode(CHG_USB100);
        else if (!strcmp(argv[1], "usb500")) power_set_charger_mode(CHG_USB500);
        else if (!strcmp(argv[1], "suspend")) power_set_charger_mode(CHG_SUSPEND);
        else if (!strcmp(argv[1], "auto")) power_apply_usb_policy();
        else goto usage;
        printf("charger %s (auto policy re-applies on the next USB check)\n", power_charger_mode_name(power_charger_mode()));
        return 0;
    }
usage:
    printf("usage: charger usb100|usb500|suspend|auto\n");
    return 1;
}

static int cmd_calib(int argc, char **argv)
{
    // Interactive end-point capture: turn the wheel to each stop when asked.
    if (argc == 2 && !strcmp(argv[1], "wheel")) {
        int mn = 1 << 30, mx = -1;
        printf("Sweep the wheel end to end several times within 8 seconds...\n");
        int64_t end = esp_timer_get_time() + 8000000;
        while (esp_timer_get_time() < end) {
            int mv = wheel_read_mv();
            if (mv >= 0) { if (mv < mn) mn = mv; if (mv > mx) mx = mv; }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        wheel_cal_t c = { .min_mv = mn + 10, .max_mv = mx - 10 };   // small dead zone at each stop
        if (c.max_mv - c.min_mv < 500) { printf("range too small (%d..%d mV); not saved\n", mn, mx); return 1; }
        wheel_set_calibration(c);
        settings_set_i32(SETTING_WHEEL_MIN_MV, c.min_mv);
        settings_set_i32(SETTING_WHEEL_MAX_MV, c.max_mv);
        printf("saved wheel %d..%d mV (raw %d..%d)\n", c.min_mv, c.max_mv, mn, mx);
        return 0;
    }
    printf("usage: calib wheel\n");
    return 1;
}

static int cmd_erase(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "yes")) {
        printf("%s\n", settings_erase_all() == ESP_OK ? "all settings erased" : "erase failed");
        return 0;
    }
    printf("usage: erase yes   (erases Wi-Fi, Spotify token and calibration)\n");
    return 1;
}

static int cmd_restart(int argc, char **argv) { esp_restart(); return 0; }

esp_err_t app_console_start(void)
{
    temperature_sensor_config_t tc = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&tc, &s_tsens) == ESP_OK) temperature_sensor_enable(s_tsens);
    else s_tsens = NULL;

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t rc = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    rc.prompt = "remote> ";
    rc.max_cmdline_length = 600;            // refresh tokens are long
    esp_console_dev_usb_serial_jtag_config_t hw = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    esp_err_t err = esp_console_new_repl_usb_serial_jtag(&hw, &rc, &repl);
    if (err != ESP_OK) return err;

    const esp_console_cmd_t cmds[] = {
        { .command = "status",  .help = "Board, power, input and settings summary", .func = cmd_status },
        { .command = "wifi",    .help = "wifi set <ssid> <password> | wifi clear", .func = cmd_wifi },
        { .command = "token",   .help = "token set <refresh_token> | token clear", .func = cmd_token },
        { .command = "charger", .help = "charger usb100|usb500|suspend|auto", .func = cmd_charger },
        { .command = "calib",   .help = "calib wheel: capture wheel end points", .func = cmd_calib },
        { .command = "erase",   .help = "erase yes: wipe all settings", .func = cmd_erase },
        { .command = "restart", .help = "Reboot", .func = cmd_restart },
    };
    for (size_t i = 0; i < sizeof cmds / sizeof cmds[0]; i++) esp_console_cmd_register(&cmds[i]);
    esp_console_register_help_command();
    return esp_console_start_repl(repl);
}
