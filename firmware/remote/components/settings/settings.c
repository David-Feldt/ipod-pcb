#include <string.h>
#include "settings.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "settings";
static const char *NS = "remote";

esp_err_t settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase (%s)", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static esp_err_t open_ns(nvs_open_mode_t mode, nvs_handle_t *h) { return nvs_open(NS, mode, h); }

esp_err_t settings_get_str(const char *key, char *out, size_t out_len)
{
    nvs_handle_t h;
    esp_err_t err = open_ns(NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return err;       // namespace not created yet
    if (err != ESP_OK) return err;
    size_t len = out_len;
    err = nvs_get_str(h, key, out, &len);
    nvs_close(h);
    return err;
}

esp_err_t settings_set_str(const char *key, const char *value)
{
    nvs_handle_t h;
    esp_err_t err = open_ns(NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, key, value);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t settings_get_i32(const char *key, int32_t *out)
{
    nvs_handle_t h;
    esp_err_t err = open_ns(NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    err = nvs_get_i32(h, key, out);
    nvs_close(h);
    return err;
}

esp_err_t settings_set_i32(const char *key, int32_t value)
{
    nvs_handle_t h;
    esp_err_t err = open_ns(NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_i32(h, key, value);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

bool settings_has(const char *key)
{
    nvs_handle_t h;
    if (open_ns(NVS_READONLY, &h) != ESP_OK) return false;
    nvs_type_t type;
    bool found = nvs_find_key(h, key, &type) == ESP_OK;
    nvs_close(h);
    return found;
}

esp_err_t settings_erase(const char *key)
{
    nvs_handle_t h;
    esp_err_t err = open_ns(NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_erase_key(h, key);
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t settings_erase_all(void)
{
    nvs_handle_t h;
    esp_err_t err = open_ns(NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_erase_all(h);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
