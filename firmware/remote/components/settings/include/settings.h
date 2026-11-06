// Small typed wrapper over NVS namespace "remote". NVS writes are atomic per
// entry, so a power cut leaves either the old or the new value.
#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define SETTING_WIFI_SSID      "wifi_ssid"
#define SETTING_WIFI_PASS      "wifi_pass"
#define SETTING_REFRESH_TOKEN  "sp_refresh"
#define SETTING_WHEEL_MIN_MV   "wheel_min"
#define SETTING_WHEEL_MAX_MV   "wheel_max"

esp_err_t settings_init(void);
esp_err_t settings_get_str(const char *key, char *out, size_t out_len);   // ESP_ERR_NVS_NOT_FOUND if unset
esp_err_t settings_set_str(const char *key, const char *value);
esp_err_t settings_get_i32(const char *key, int32_t *out);
esp_err_t settings_set_i32(const char *key, int32_t value);
bool settings_has(const char *key);
esp_err_t settings_erase(const char *key);
esp_err_t settings_erase_all(void);
