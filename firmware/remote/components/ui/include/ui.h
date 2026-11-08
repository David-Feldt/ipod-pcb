// LVGL on the e-paper. One UI task owns LVGL; other tasks must wrap any lv_*
// or screen call in ui_lock()/ui_unlock().
//
// Refresh policy (FIRMWARE-NOTES.md, accepted 2026-10-20): coalesce changes,
// never overlap refreshes, always show the latest state rather than queueing
// stale frames, partial updates by default, and a full clearing refresh after
// CONFIG_REMOTE_FULL_REFRESH_EVERY partials or when requested (track change,
// wake, reset).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t ui_init(void);
void ui_lock(void);
void ui_unlock(void);
void ui_request_full_refresh(void);     // next refresh is full, even if nothing changed

// Screen calibration: pixels hidden by the bezel/case on each edge of the
// 122 x 250 panel. Stored in NVS; layouts use only the visible area.
typedef struct { int left, right, top, bottom; } ui_insets_t;
#define UI_INSET_MAX 30

void ui_show_calibration(void);         // concentric 1 px frames every 3 px; frame 6 (15 px) is thick

// ---------------------------------------------------------------- now playing
typedef struct {
    int battery_pct;        // < 0 hides it
    bool usb;               // USB power present
    bool wifi;              // connected
} ui_status_t;

void np_create(const ui_insets_t *in);  // build and load the now-playing screen
void np_show(void);                     // reload it (e.g. leaving calibration)
void np_set_status(const ui_status_t *s);
void np_set_track(const char *title, const char *artist, const char *album, int duration_ms);
void np_set_progress(int progress_ms);  // only redraws when a pixel would change
void np_set_playing(bool playing);
#define UI_ART_MAX 110
int np_art_size(void);                  // edge of the album-art square in pixels
void np_set_art(const uint8_t *bits);   // packed 1-bit, ((size+7)/8)*size bytes, 1 = white; NULL = placeholder
void np_show_volume(int pct);           // transient overlay
void np_show_notice(const char *msg);   // transient one-line notice in the same overlay
void np_show_message(const char *msg);  // replaces track text (e.g. "Nothing playing"); NULL clears
