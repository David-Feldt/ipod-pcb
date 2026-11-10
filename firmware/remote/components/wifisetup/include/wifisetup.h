// Wi-Fi setup hotspot ("captive portal").
//
// The board runs its own WPA2 network ("E-ink Remote XXXX") with a fresh
// random password; the screen shows it as a Wi-Fi QR code, so joining is one
// scan. A tiny DNS server answers every lookup with 192.168.4.1, so the phone
// opens the setup page by itself. The page lists nearby networks; the board
// tries the chosen one while the hotspot stays up, saves it only if it works,
// then shuts the hotspot down.
//
// Started automatically at boot when no Wi-Fi is saved, or from the menu.
// Stops after success, on wifisetup_stop(), or after WIFISETUP_TIMEOUT_MIN idle
// minutes. A saved network that is merely unreachable never starts it.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#define WIFISETUP_TIMEOUT_MIN 10

typedef enum {
    WS_OFF,
    WS_WAITING,         // hotspot up; waiting for a network to be chosen
    WS_CONNECTING,      // trying the chosen network
    WS_DONE,            // joined and saved; stops by itself shortly
} ws_state_t;

typedef struct {
    ws_state_t state;
    char ap_ssid[24];       // hotspot name
    char ap_pass[16];       // hotspot password
    char target[33];        // network being tried / joined
    char ip[16];            // address on that network once joined
    char error[96];         // last failure while WAITING ("" if none)
} wifisetup_info_t;

esp_err_t wifisetup_start(void);
void wifisetup_stop(void);
bool wifisetup_active(void);
uint32_t wifisetup_version(void);
void wifisetup_get(wifisetup_info_t *out);

// "WIFI:T:WPA;S:<ssid>;P:<pass>;;" for a join QR code.
void wifisetup_join_qr(const wifisetup_info_t *i, char *out, size_t n);
