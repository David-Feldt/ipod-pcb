#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "spotify.h"
#include "settings.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "spotify";

#define TOKEN_URL       "https://accounts.spotify.com/api/token"
#define API_BASE        "https://api.spotify.com"
#define MAX_BODY        (32 * 1024)     // responses we parse are a few KB
#define REFRESH_MARGIN  (120 * 1000000LL)   // refresh 2 min before expiry
#define TOKEN_MAX       512
#define HTTP_TIMEOUT_MS 10000

typedef struct {
    char *buf;
    size_t len, cap;
    bool overflow;
    int retry_after_s;
} collector_t;

static SemaphoreHandle_t s_lock;
static esp_http_client_handle_t s_api;     // kept open: one TLS session to api.spotify.com
static collector_t s_col;
static char s_access[TOKEN_MAX];
static int64_t s_expires_us;               // esp_timer time when s_access expires
static sp_auth_t s_auth = SP_AUTH_NO_TOKEN;

static esp_err_t on_http(esp_http_client_event_t *e)
{
    collector_t *c = e->user_data;
    if (!c) return ESP_OK;
    if (e->event_id == HTTP_EVENT_ON_HEADER && !strcasecmp(e->header_key, "Retry-After")) {
        c->retry_after_s = atoi(e->header_value);
    } else if (e->event_id == HTTP_EVENT_ON_DATA && e->data_len > 0) {
        if (c->len + e->data_len > MAX_BODY) { c->overflow = true; return ESP_OK; }
        if (c->len + e->data_len + 1 > c->cap) {
            size_t cap = c->cap ? c->cap : 1024;
            while (cap < c->len + e->data_len + 1) cap *= 2;
            char *nb = realloc(c->buf, cap);
            if (!nb) { c->overflow = true; return ESP_OK; }
            c->buf = nb;
            c->cap = cap;
        }
        memcpy(c->buf + c->len, e->data, e->data_len);
        c->len += e->data_len;
        c->buf[c->len] = 0;
    }
    return ESP_OK;
}

static void collector_reset(collector_t *c)
{
    free(c->buf);
    memset(c, 0, sizeof *c);
}

// Hand the collected body to resp (resp takes ownership).
static void collector_take(collector_t *c, sp_response_t *resp)
{
    resp->body = c->buf ? c->buf : strdup("");
    resp->len = c->len;
    resp->retry_after_s = c->retry_after_s;
    c->buf = NULL;
    collector_reset(c);
}

static void url_encode(char *out, size_t n, const char *in)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (; *in && o + 4 < n; in++) {
        unsigned char ch = *in;
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            out[o++] = ch;
        } else {
            out[o++] = '%'; out[o++] = hex[ch >> 4]; out[o++] = hex[ch & 15];
        }
    }
    out[o] = 0;
}

static esp_err_t refresh_locked(bool *rotated)
{
    if (rotated) *rotated = false;
    static char refresh[TOKEN_MAX];     // static: keeps TLS-task stacks small; guarded by s_lock
    if (settings_get_str(SETTING_REFRESH_TOKEN, refresh, sizeof refresh) != ESP_OK || !refresh[0]) {
        s_auth = SP_AUTH_NO_TOKEN;
        return ESP_ERR_NOT_FOUND;
    }
    if (!CONFIG_REMOTE_SPOTIFY_CLIENT_ID[0]) {
        ESP_LOGE(TAG, "CONFIG_REMOTE_SPOTIFY_CLIENT_ID is empty (idf.py menuconfig)");
        s_auth = SP_AUTH_ERROR;
        return ESP_ERR_INVALID_STATE;
    }

    static char enc[TOKEN_MAX * 3];
    url_encode(enc, sizeof enc, refresh);
    size_t blen = strlen(enc) + 128;
    char *body = malloc(blen);
    if (!body) return ESP_ERR_NO_MEM;
    snprintf(body, blen, "grant_type=refresh_token&refresh_token=%s&client_id=%s", enc,
             CONFIG_REMOTE_SPOTIFY_CLIENT_ID);

    collector_t col = { 0 };
    esp_http_client_config_t cfg = {
        .url = TOKEN_URL, .method = HTTP_METHOD_POST, .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach, .event_handler = on_http, .user_data = &col,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) { free(body); return ESP_ERR_NO_MEM; }
    esp_http_client_set_header(c, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(c, body, strlen(body));
    esp_err_t err = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    free(body);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "token request failed: %s", esp_err_to_name(err));
        s_auth = SP_AUTH_ERROR;
        collector_reset(&col);
        return err;
    }
    cJSON *j = col.buf ? cJSON_Parse(col.buf) : NULL;
    if (status != 200 || !j) {
        const char *e = j ? cJSON_GetStringValue(cJSON_GetObjectItem(j, "error")) : NULL;
        ESP_LOGW(TAG, "token refresh HTTP %d (%s)", status, e ? e : "no detail");
        // invalid_grant: revoked, expired or already-rotated token. Needs pairing again.
        s_auth = (status == 400 && e && !strcmp(e, "invalid_grant")) ? SP_AUTH_REJECTED : SP_AUTH_ERROR;
        cJSON_Delete(j);
        collector_reset(&col);
        return ESP_FAIL;
    }

    const char *access = cJSON_GetStringValue(cJSON_GetObjectItem(j, "access_token"));
    const char *new_refresh = cJSON_GetStringValue(cJSON_GetObjectItem(j, "refresh_token"));
    cJSON *exp = cJSON_GetObjectItem(j, "expires_in");
    if (!access || strlen(access) >= sizeof s_access) {
        ESP_LOGW(TAG, "token response missing access_token");
        s_auth = SP_AUTH_ERROR;
        cJSON_Delete(j);
        collector_reset(&col);
        return ESP_FAIL;
    }
    strcpy(s_access, access);
    int expires_s = cJSON_IsNumber(exp) ? exp->valueint : 3600;
    s_expires_us = esp_timer_get_time() + (int64_t)expires_s * 1000000;

    // Rotation: save the new refresh token before anything else can fail.
    if (new_refresh && new_refresh[0] && strcmp(new_refresh, refresh) && strlen(new_refresh) < TOKEN_MAX) {
        if (settings_set_str(SETTING_REFRESH_TOKEN, new_refresh) == ESP_OK) {
            if (rotated) *rotated = true;
            ESP_LOGI(TAG, "refresh token rotated and saved");
        } else {
            ESP_LOGE(TAG, "could not save rotated refresh token");
        }
    }
    s_auth = SP_AUTH_OK;
    ESP_LOGI(TAG, "access token valid for %d s", expires_s);
    cJSON_Delete(j);
    collector_reset(&col);
    return ESP_OK;
}

static esp_err_t ensure_token_locked(void)
{
    if (s_access[0] && esp_timer_get_time() < s_expires_us - REFRESH_MARGIN) return ESP_OK;
    return refresh_locked(NULL);
}

static void api_close_locked(void)
{
    if (s_api) esp_http_client_cleanup(s_api);
    s_api = NULL;
}

static esp_err_t api_perform_locked(const char *method, const char *path, const char *json_body,
                                    sp_response_t *resp)
{
    char url[256];
    snprintf(url, sizeof url, API_BASE "%s", path);
    if (!s_api) {
        esp_http_client_config_t cfg = {
            .url = url, .timeout_ms = HTTP_TIMEOUT_MS, .crt_bundle_attach = esp_crt_bundle_attach,
            .event_handler = on_http, .user_data = &s_col, .keep_alive_enable = true,
            .buffer_size = 2048, .buffer_size_tx = 1024,
        };
        s_api = esp_http_client_init(&cfg);
        if (!s_api) return ESP_ERR_NO_MEM;
    }
    esp_http_client_method_t m = HTTP_METHOD_GET;
    if (!strcmp(method, "PUT")) m = HTTP_METHOD_PUT;
    else if (!strcmp(method, "POST")) m = HTTP_METHOD_POST;
    else if (!strcmp(method, "DELETE")) m = HTTP_METHOD_DELETE;

    static char auth[TOKEN_MAX + 8];
    snprintf(auth, sizeof auth, "Bearer %s", s_access);
    esp_http_client_set_url(s_api, url);
    esp_http_client_set_method(s_api, m);
    esp_http_client_set_header(s_api, "Authorization", auth);
    if (json_body) {
        esp_http_client_set_header(s_api, "Content-Type", "application/json");
        esp_http_client_set_post_field(s_api, json_body, strlen(json_body));
    } else {
        esp_http_client_set_post_field(s_api, NULL, 0);
    }
    collector_reset(&s_col);
    esp_err_t err = esp_http_client_perform(s_api);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s %s failed: %s", method, path, esp_err_to_name(err));
        collector_reset(&s_col);
        api_close_locked();             // start a fresh connection next time
        return err;
    }
    resp->status = esp_http_client_get_status_code(s_api);
    if (s_col.overflow) ESP_LOGW(TAG, "%s %s: response truncated", method, path);
    collector_take(&s_col, resp);
    return ESP_OK;
}

esp_err_t spotify_request(const char *method, const char *path, const char *json_body, sp_response_t *resp)
{
    memset(resp, 0, sizeof *resp);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ensure_token_locked();
    if (err == ESP_OK) {
        err = api_perform_locked(method, path, json_body, resp);
        if (err == ESP_OK && resp->status == 401) {
            // Access token revoked or expired early: refresh once and retry.
            spotify_response_free(resp);
            s_access[0] = 0;
            if ((err = refresh_locked(NULL)) == ESP_OK) err = api_perform_locked(method, path, json_body, resp);
        }
    }
    xSemaphoreGive(s_lock);
    return err;
}

void spotify_response_free(sp_response_t *resp)
{
    free(resp->body);
    memset(resp, 0, sizeof *resp);
}

esp_err_t spotify_refresh(bool *rotated)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = refresh_locked(rotated);
    xSemaphoreGive(s_lock);
    return err;
}

void spotify_reset(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_access[0] = 0;
    s_expires_us = 0;
    api_close_locked();
    s_auth = settings_has(SETTING_REFRESH_TOKEN) ? SP_AUTH_UNKNOWN : SP_AUTH_NO_TOKEN;
    xSemaphoreGive(s_lock);
}

sp_auth_t spotify_auth_state(void) { return s_auth; }

const char *spotify_auth_name(sp_auth_t a)
{
    static const char *n[] = { "no token (pair the device)", "not tried yet", "ok", "rejected (pair again)",
                               "error (will retry)" };
    return a <= SP_AUTH_ERROR ? n[a] : "?";
}

int spotify_token_ttl_s(void)
{
    if (!s_access[0]) return 0;
    int64_t left = s_expires_us - esp_timer_get_time();
    return left > 0 ? (int)(left / 1000000) : 0;
}

esp_err_t spotify_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    s_auth = settings_has(SETTING_REFRESH_TOKEN) ? SP_AUTH_UNKNOWN : SP_AUTH_NO_TOKEN;
    return ESP_OK;
}
