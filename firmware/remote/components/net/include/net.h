// Wi-Fi station and SNTP. Credentials come from settings (console `wifi set`
// or the Wi-Fi setup hotspot). Reconnects on its own with backoff.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

typedef enum {
    NET_NO_CREDENTIALS,     // nothing saved; pairing needed
    NET_CONNECTING,
    NET_CONNECTED,          // associated and has an IP address
    NET_DISCONNECTED,       // lost or failed; retrying
} net_state_t;

// Bring up the Wi-Fi stack and connect with the saved credentials, if any.
esp_err_t net_start(void);

// Re-read the saved credentials and reconnect (after `wifi set` / `wifi clear`).
esp_err_t net_reload(void);

net_state_t net_state(void);
const char *net_state_name(net_state_t s);
bool net_connected(void);
bool net_wait_connected(TickType_t timeout);

// True once SNTP has set the clock at least once since boot.
bool net_time_valid(void);

// ---------------------------------------------------------------- hotspot
// Wi-Fi setup: run our own access point (APSTA, 192.168.4.1) while holding the
// station: no automatic (re)connects until net_ap_stop(), which returns to the
// saved network unless net_try() already connected.
esp_err_t net_ap_start(const char *ssid, const char *pass);   // pass "" = open, else >= 8 chars
void net_ap_stop(void);
bool net_ap_active(void);

typedef struct {
    char ssid[33];
    int rssi;
    bool open;
} net_scan_entry_t;

// Blocking scan (about 2-3 s); unique SSIDs, strongest first.
int net_scan(net_scan_entry_t *out, int max);

typedef enum { NET_TRY_OK, NET_TRY_WRONG_PASSWORD, NET_TRY_NOT_FOUND, NET_TRY_FAILED } net_try_t;

// While the hotspot is up: join this network (stays connected on success,
// with SNTP). Does not save anything. Blocks up to timeout_ms.
net_try_t net_try(const char *ssid, const char *pass, int timeout_ms);

bool net_have_credentials(void);

// Bluetooth mode only (net_start() not called): bring the Wi-Fi driver up just
// long enough to scan for the saved network, then stop it. Blocks ~1-3 s.
bool net_probe_saved(int *rssi);

// Current IP as text ("" when not connected) and signal strength (0 if unknown).
void net_ip_str(char *out, size_t n);
int net_rssi(void);
