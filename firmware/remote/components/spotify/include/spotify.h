// Spotify Web API client: PKCE refresh-token flow and authenticated requests.
// The refresh token lives in settings (SETTING_REFRESH_TOKEN). Spotify may
// return a new one on each refresh; it is saved immediately, because the old
// one can stop working.
//
// All functions block (network I/O) and are serialized internally; call them
// from a task with at least 8 KB of stack (TLS).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

typedef enum {
    SP_AUTH_NO_TOKEN,       // no refresh token saved: pair the device
    SP_AUTH_UNKNOWN,        // token saved, not yet tried this boot
    SP_AUTH_OK,             // holding a valid access token
    SP_AUTH_REJECTED,       // Spotify rejected the refresh token: pair again
    SP_AUTH_ERROR,          // network or server error; will retry
} sp_auth_t;

typedef struct {
    int status;             // HTTP status, or 0 if the request never completed
    char *body;             // NUL-terminated response body (may be empty), heap
    size_t len;
    int retry_after_s;      // from a 429 response
} sp_response_t;

esp_err_t spotify_init(void);

sp_auth_t spotify_auth_state(void);
const char *spotify_auth_name(sp_auth_t a);

// Seconds until the current access token expires (0 if none).
int spotify_token_ttl_s(void);

// Force a token refresh now. *rotated (optional) tells whether Spotify issued
// a new refresh token (which has been saved).
esp_err_t spotify_refresh(bool *rotated);

// First sign-in (pairing): exchange a PKCE authorization code for tokens and
// save the refresh token. redirect_uri must equal the one used to authorize.
// Replaces any previous login. On failure the old login (if any) stays saved.
esp_err_t spotify_exchange_code(const char *code, const char *verifier, const char *redirect_uri);

// Forget the cached access token (after `token set` / `token clear`).
void spotify_reset(void);

// Authenticated request to https://api.spotify.com<path>. json_body may be
// NULL. Refreshes the access token when needed and retries once on 401.
// Returns ESP_OK if an HTTP response arrived (check resp->status).
// Always call spotify_response_free afterwards.
esp_err_t spotify_request(const char *method, const char *path, const char *json_body, sp_response_t *resp);
void spotify_response_free(sp_response_t *resp);
