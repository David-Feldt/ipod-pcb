// Wi-Fi station and SNTP. Credentials come from settings (console `wifi set`,
// later the pairing portal). Reconnects on its own with backoff.
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

// Current IP as text ("" when not connected) and signal strength (0 if unknown).
void net_ip_str(char *out, size_t n);
int net_rssi(void);
