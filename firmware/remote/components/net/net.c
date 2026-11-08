#include <string.h>
#include <stdio.h>
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
#define RETRY_MIN_MS   1000
#define RETRY_MAX_MS   60000

static EventGroupHandle_t s_ev;
static esp_netif_t *s_netif;
static TimerHandle_t s_retry;
static volatile net_state_t s_state = NET_NO_CREDENTIALS;
static uint32_t s_retry_ms = RETRY_MIN_MS;
static bool s_sntp_started;

static void retry_cb(TimerHandle_t t)
{
    if (s_state == NET_NO_CREDENTIALS) return;
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
