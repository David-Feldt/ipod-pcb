#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "net.h"
#include "settings.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"

static const char *TAG = "net";

#define BIT_CONNECTED  BIT0
#define BIT_TIME       BIT1
#define BIT_FAILED     BIT2     // a station attempt failed while held (net_try)
#define RETRY_MIN_MS   1000
#define RETRY_MAX_MS   60000

static EventGroupHandle_t s_ev;
static esp_netif_t *s_netif;
static TimerHandle_t s_retry;
static volatile net_state_t s_state = NET_NO_CREDENTIALS;
static uint32_t s_retry_ms = RETRY_MIN_MS;
static bool s_sntp_started;
static esp_netif_t *s_ap_netif;
static volatile bool s_held;             // hotspot up: no automatic station (re)connects
static volatile uint8_t s_last_reason;

static void retry_cb(TimerHandle_t t)
{
    if (s_state == NET_NO_CREDENTIALS || s_held) return;
    s_state = NET_CONNECTING;
    esp_wifi_connect();
}

static void time_synced(struct timeval *tv)
{
    xEventGroupSetBits(s_ev, BIT_TIME);
    ESP_LOGI(TAG, "clock set by SNTP");
}

static void start_sntp(void)
{
    if (s_sntp_started) return;
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.sync_cb = time_synced;
    if (esp_netif_sntp_init(&cfg) == ESP_OK) s_sntp_started = true;
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = data;
        xEventGroupClearBits(s_ev, BIT_CONNECTED);
        if (s_held) {                   // net_try() handles it
            s_last_reason = d->reason;
            s_state = NET_DISCONNECTED;
            xEventGroupSetBits(s_ev, BIT_FAILED);
            return;
        }
        // Our own esp_wifi_disconnect() (credential change) is not a failure.
        if (s_state == NET_NO_CREDENTIALS || d->reason == WIFI_REASON_ASSOC_LEAVE) return;
        ESP_LOGW(TAG, "disconnected (reason %d); retry in %lu ms", d->reason, (unsigned long)s_retry_ms);
        s_state = NET_DISCONNECTED;
        xTimerChangePeriod(s_retry, pdMS_TO_TICKS(s_retry_ms), 0);
        xTimerStart(s_retry, 0);
        s_retry_ms = s_retry_ms * 2 > RETRY_MAX_MS ? RETRY_MAX_MS : s_retry_ms * 2;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "connected, ip " IPSTR, IP2STR(&e->ip_info.ip));
        s_state = NET_CONNECTED;
        s_retry_ms = RETRY_MIN_MS;
        xEventGroupSetBits(s_ev, BIT_CONNECTED);
        start_sntp();
    }
}

esp_err_t net_reload(void)
{
    wifi_config_t wc = { 0 };
    char ssid[33] = "", pass[65] = "";
    bool have = settings_get_str(SETTING_WIFI_SSID, ssid, sizeof ssid) == ESP_OK && ssid[0];
    settings_get_str(SETTING_WIFI_PASS, pass, sizeof pass);

    s_state = NET_NO_CREDENTIALS;     // suppress auto-retry while switching
    xTimerStop(s_retry, 0);
    esp_wifi_disconnect();
    if (s_held) return ESP_OK;          // the hotspot owns the station; applied on net_ap_stop()
    if (!have) {
        ESP_LOGW(TAG, "no Wi-Fi credentials saved");
        return ESP_ERR_NOT_FOUND;
    }
    memcpy(wc.sta.ssid, ssid, strlen(ssid));
    memcpy(wc.sta.password, pass, strlen(pass));
    wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "connecting to \"%s\"", ssid);
    s_retry_ms = RETRY_MIN_MS;
    s_state = NET_CONNECTING;
    return esp_wifi_connect();
}

esp_err_t net_start(void)
{
    s_ev = xEventGroupCreate();
    s_retry = xTimerCreate("wifi_retry", pdMS_TO_TICKS(RETRY_MIN_MS), pdFALSE, NULL, retry_cb);
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    s_netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(s_netif, "eink-remote");

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if ((err = esp_wifi_init(&init)) != ESP_OK) return err;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);          // credentials live in our own settings
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
    esp_wifi_set_mode(WIFI_MODE_STA);
    if ((err = esp_wifi_start()) != ESP_OK) return err;
    net_reload();
    return ESP_OK;
}

// ------------------------------------------------------------------ hotspot

esp_err_t net_ap_start(const char *ssid, const char *pass)
{
    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();      // 192.168.4.1 with a DHCP server
        if (!s_ap_netif) return ESP_ERR_NO_MEM;
        // DHCP option 114 (RFC 8910): tells phones where the setup page is.
        static const char uri[] = "http://192.168.4.1/";
        esp_netif_dhcps_stop(s_ap_netif);
        esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, (void *)uri, strlen(uri));
        esp_netif_dhcps_start(s_ap_netif);
    }
    s_held = true;
    xTimerStop(s_retry, 0);
    esp_wifi_disconnect();
    s_state = net_have_credentials() ? NET_DISCONNECTED : NET_NO_CREDENTIALS;

    wifi_config_t ac = { 0 };
    size_t sl = strlen(ssid), pl = strlen(pass);
    if (sl > sizeof ac.ap.ssid || pl >= sizeof ac.ap.password || (pl && pl < 8)) return ESP_ERR_INVALID_ARG;
    memcpy(ac.ap.ssid, ssid, sl);
    ac.ap.ssid_len = sl;
    memcpy(ac.ap.password, pass, pl);
    ac.ap.authmode = pl ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ac.ap.channel = 1;
    ac.ap.max_connection = 2;
    ac.ap.pmf_cfg.capable = true;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &ac);
    if (err != ESP_OK) { net_ap_stop(); return err; }
    ESP_LOGI(TAG, "hotspot \"%s\" up", ssid);
    return ESP_OK;
}

void net_ap_stop(void)
{
    if (!s_held) return;
    esp_wifi_set_mode(WIFI_MODE_STA);
    s_held = false;
    ESP_LOGI(TAG, "hotspot down");
    if (!net_connected()) net_reload();         // back to the saved network
}

bool net_ap_active(void) { return s_held; }

int net_scan(net_scan_entry_t *out, int max)
{
    wifi_scan_config_t sc = { .show_hidden = false };
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) return 0;
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n > 40) n = 40;
    wifi_ap_record_t *rec = calloc(n ? n : 1, sizeof *rec);
    if (!rec) { esp_wifi_clear_ap_list(); return 0; }
    esp_wifi_scan_get_ap_records(&n, rec);          // sorted by signal, strongest first
    int k = 0;
    for (int i = 0; i < n && k < max; i++) {
        if (!rec[i].ssid[0]) continue;
        bool dup = false;
        for (int j = 0; j < k && !dup; j++) dup = !strcmp(out[j].ssid, (char *)rec[i].ssid);
        if (dup) continue;
        snprintf(out[k].ssid, sizeof out[k].ssid, "%s", (char *)rec[i].ssid);
        out[k].rssi = rec[i].rssi;
        out[k].open = rec[i].authmode == WIFI_AUTH_OPEN;
        k++;
    }
    free(rec);
    return k;
}

static bool reason_is_password(uint8_t r)
{
    return r == WIFI_REASON_AUTH_FAIL || r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
           r == WIFI_REASON_HANDSHAKE_TIMEOUT || r == WIFI_REASON_MIC_FAILURE;
}

static bool reason_is_not_found(uint8_t r)
{
    return r == WIFI_REASON_NO_AP_FOUND || r == WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY ||
           r == WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD || r == WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD;
}

net_try_t net_try(const char *ssid, const char *pass, int timeout_ms)
{
    if (!s_held) return NET_TRY_FAILED;
    wifi_config_t wc = { 0 };
    size_t sl = strlen(ssid), pl = strlen(pass);
    if (!sl || sl > sizeof wc.sta.ssid || pl >= sizeof wc.sta.password) return NET_TRY_FAILED;
    memcpy(wc.sta.ssid, ssid, sl);
    memcpy(wc.sta.password, pass, pl);
    wc.sta.threshold.authmode = pl ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    esp_wifi_disconnect();
    if (esp_wifi_set_config(WIFI_IF_STA, &wc) != ESP_OK) return NET_TRY_FAILED;

    // Up to three attempts: one handshake timeout can be bad luck, two in a
    // row with nothing else is a wrong password.
    int password_fails = 0, not_found = 0;
    TickType_t end = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    for (int attempt = 0; attempt < 3; attempt++) {
        TickType_t now = xTaskGetTickCount();
        if (now >= end) break;
        xEventGroupClearBits(s_ev, BIT_FAILED | BIT_CONNECTED);
        s_state = NET_CONNECTING;
        ESP_LOGI(TAG, "trying \"%s\" (attempt %d)", ssid, attempt + 1);
        esp_wifi_connect();
        EventBits_t b = xEventGroupWaitBits(s_ev, BIT_CONNECTED | BIT_FAILED, pdFALSE, pdFALSE, end - now);
        if (b & BIT_CONNECTED) return NET_TRY_OK;
        if (!(b & BIT_FAILED)) break;                   // timed out
        uint8_t r = s_last_reason;
        ESP_LOGW(TAG, "attempt failed, reason %d", r);
        if (reason_is_password(r) && ++password_fails >= 2) break;
        if (reason_is_not_found(r) && ++not_found >= 2) break;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    esp_wifi_disconnect();
    s_state = NET_DISCONNECTED;
    if (password_fails >= 2) return NET_TRY_WRONG_PASSWORD;
    if (not_found >= 2) return NET_TRY_NOT_FOUND;
    return NET_TRY_FAILED;
}

bool net_have_credentials(void)
{
    char ssid[33] = "";
    return settings_get_str(SETTING_WIFI_SSID, ssid, sizeof ssid) == ESP_OK && ssid[0];
}

net_state_t net_state(void) { return s_state; }

const char *net_state_name(net_state_t s)
{
    static const char *n[] = { "no credentials", "connecting", "connected", "disconnected (retrying)" };
    return s <= NET_DISCONNECTED ? n[s] : "?";
}

bool net_connected(void) { return s_ev && (xEventGroupGetBits(s_ev) & BIT_CONNECTED); }

bool net_wait_connected(TickType_t timeout)
{
    return s_ev && (xEventGroupWaitBits(s_ev, BIT_CONNECTED, pdFALSE, pdTRUE, timeout) & BIT_CONNECTED);
}

bool net_time_valid(void) { return s_ev && (xEventGroupGetBits(s_ev) & BIT_TIME); }

void net_ip_str(char *out, size_t n)
{
    out[0] = 0;
    esp_netif_ip_info_t ip;
    if (net_connected() && esp_netif_get_ip_info(s_netif, &ip) == ESP_OK)
        snprintf(out, n, IPSTR, IP2STR(&ip.ip));
}

int net_rssi(void)
{
    wifi_ap_record_t ap;
    return net_connected() && esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
}

// ------------------------------------------------------------------ probe
bool net_probe_saved(int *rssi)
{
    char ssid[33];
    if (settings_get_str(SETTING_WIFI_SSID, ssid, sizeof ssid) != ESP_OK || !ssid[0]) return false;
    static bool ready;
    if (!ready) {
        esp_err_t err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;
        wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
        if (esp_wifi_init(&init) != ESP_OK) return false;
        esp_wifi_set_storage(WIFI_STORAGE_RAM);
        esp_wifi_set_mode(WIFI_MODE_STA);
        ready = true;
    }
    if (esp_wifi_start() != ESP_OK) return false;
    wifi_scan_config_t sc = { .ssid = (uint8_t *)ssid };
    bool found = false;
    if (esp_wifi_scan_start(&sc, true) == ESP_OK) {
        uint16_t n = 1;
        wifi_ap_record_t rec;
        if (esp_wifi_scan_get_ap_records(&n, &rec) == ESP_OK && n > 0) {
            found = true;
            if (rssi) *rssi = rec.rssi;
        }
        esp_wifi_clear_ap_list();
    }
    esp_wifi_stop();
    ESP_LOGI(TAG, "probe for \"%s\": %s", ssid, found ? "in range" : "not found");
    return found;
}
