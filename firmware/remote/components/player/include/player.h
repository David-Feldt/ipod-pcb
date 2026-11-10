// Spotify playback state and control. One task owns all Spotify traffic: it
// polls GET /v1/me/player (fast while playing, slow otherwise), runs button
// commands, and rate-limits volume changes from the wheel. Commands update the
// local state optimistically so the screen can react before Spotify confirms.
//
// The app reads the state with player_get() whenever player_version() changes.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    PLAYER_STARTING,        // no answer from Spotify yet
    PLAYER_OK,              // state below is live
    PLAYER_IDLE,            // signed in, but nothing is playing on any device
    PLAYER_NO_WIFI,
    PLAYER_NO_TOKEN,        // never signed in
    PLAYER_REJECTED,        // Spotify rejected the saved login
    PLAYER_ERROR,           // network/server trouble; retrying
} player_status_t;

typedef struct {
    player_status_t status;
    bool playing;
    char track_id[64];      // changes when the track changes
    char title[128], artist[128], album[128];
    char image_url[128];    // smallest album image (for album art later)
    int duration_ms;
    int progress_ms;        // at progress_at_us (esp_timer time)
    int64_t progress_at_us;
    char device[64];
    int volume;             // 0..100, -1 if the device reports none
    bool volume_allowed;    // device accepts volume changes
} player_state_t;

esp_err_t player_start(void);

// Increments whenever the state (or a notice) changes.
uint32_t player_version(void);
void player_get(player_state_t *out);

// Playback position now, extrapolated from the last poll.
int player_position_ms(const player_state_t *s);

// Short user-facing message from the last failed command ("" if none);
// cleared when read.
void player_take_notice(char *out, size_t n);

void player_toggle(void);
void player_next(void);
void player_prev(void);              // restart if > 3 s in, else previous track
void player_set_volume(int pct);     // coalesced; sent at most every 300 ms
void player_poll_soon(void);         // poll now (e.g. right after signing in)

const char *player_status_name(player_status_t s);
