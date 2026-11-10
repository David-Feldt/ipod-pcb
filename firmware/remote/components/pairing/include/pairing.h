// Spotify sign-in from a phone or computer on the same Wi-Fi.
//
// While pairing is active the board serves a small setup page on port 80.
// "Sign in with Spotify" there goes to /login, which starts a PKCE
// authorization and sends the browser to Spotify. Spotify only redirects to
// HTTPS (or loopback) URLs, so it returns to a static bounce page
// (pairing/index.html) that forwards ?code&state to http://<device-ip>/callback;
// the device IP travels inside `state`. The board then exchanges the code
// itself and saves the refresh token. The verifier never leaves the board.
//
// The server only runs while pairing is active (it stops after success, on
// pairing_stop(), or after PAIRING_TIMEOUT_MIN idle minutes), so there is no
// always-on endpoint on the LAN that could swap the account.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define PAIRING_TIMEOUT_MIN 10

typedef enum {
    PAIR_OFF,
    PAIR_WAITING,       // server up; waiting for the user to sign in
    PAIR_SIGNING_IN,    // exchanging the authorization code
    PAIR_DONE,          // signed in; stops by itself a few seconds later
} pair_state_t;

typedef struct {
    pair_state_t state;
    char ip[16];        // device address shown to the user
    char error[96];     // last failure while WAITING ("" if none)
} pairing_info_t;

// Start the setup server. ESP_ERR_INVALID_STATE without Wi-Fi.
esp_err_t pairing_start(void);
void pairing_stop(void);
bool pairing_active(void);

// Increments on every state change.
uint32_t pairing_version(void);
void pairing_get(pairing_info_t *out);

// Redirect URI sent to Spotify: CONFIG_REMOTE_PAIRING_REDIRECT_URI, or the
// development bounce page on http://127.0.0.1:8888/callback when unset.
const char *pairing_redirect_uri(void);
