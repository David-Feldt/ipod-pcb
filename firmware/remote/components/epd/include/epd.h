// GDEY0213B74 (SSD1680) e-paper driver, 4-wire SPI.
// Framebuffer: native portrait, 122 x 250, row-major, EPD_STRIDE bytes per row,
// MSB = leftmost pixel, bit 1 = white. Row 0 is the FPC-tail (top) edge.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define EPD_WIDTH   122
#define EPD_HEIGHT  250
#define EPD_STRIDE  16                          // (122 + 7) / 8
#define EPD_FB_SIZE (EPD_STRIDE * EPD_HEIGHT)   // 4000 bytes

typedef enum {
    EPD_REFRESH_FULL,       // ~1.7 s, flashes, clears ghosting
    EPD_REFRESH_PARTIAL,    // ~0.4 s differential update against the previous frame
} epd_refresh_t;

typedef struct {
    uint32_t full_count, partial_count, timeouts;
    uint32_t last_ms;               // duration of the last refresh (BUSY time)
    epd_refresh_t last_mode;
} epd_stats_t;

typedef enum {
    EPD_ORIENT_NATIVE = 0,  // framebuffer row 0 at the FPC-tail edge
    EPD_ORIENT_ROT180,      // upside down (both axes reversed); text stays readable
    EPD_ORIENT_FLIP_V,      // mirror top <-> bottom
    EPD_ORIENT_FLIP_H,      // mirror left <-> right
} epd_orient_t;

esp_err_t epd_init(void);

// How the framebuffer maps onto the glass. Takes effect on the next update,
// which is forced to be a full refresh.
void epd_set_orientation(epd_orient_t o);
epd_orient_t epd_orientation(void);
const char *epd_orientation_name(epd_orient_t o);

// Write fb and refresh. Partial is promoted to full when the controller has no
// valid previous frame (first update, after sleep or after a fault).
// Blocks until BUSY clears or the bounded timeout expires.
esp_err_t epd_update(const uint8_t *fb, epd_refresh_t mode);

// Turn off the panel's analog supply (kept on between partial refreshes).
esp_err_t epd_power_off(void);

// Controller deep sleep; RAM is lost. The next update re-initializes and is full.
esp_err_t epd_sleep(void);

epd_stats_t epd_stats(void);
