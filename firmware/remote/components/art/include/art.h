// Album art: downloads a cover JPEG, decodes it with the ESP32-S3 ROM TJpgDec
// (streaming, ~4 KB work area, no PSRAM needed), scales it to a square, then
// stretches contrast and Atkinson-dithers it to pure black/white.
//
// Output is packed 1 bit per pixel (ART_STRIDE bytes per row, MSB = leftmost,
// 1 = white), so the copies passed around stay ~1.5 KB. Requests are handled
// by one background task; the newest request wins.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define ART_MAX_SIZE 128
#define ART_STRIDE(size) (((size) + 7) / 8)
#define ART_BYTES(size)  (ART_STRIDE(size) * (size))

// size: output edge in pixels (<= ART_MAX_SIZE).
esp_err_t art_start(int size);

// Fetch and convert this URL ("" or NULL clears the art).
void art_request(const char *url);

// Increments when a request finishes (successfully or not).
uint32_t art_version(void);

// Copy the latest result. Returns true if *pixels holds art for *url_out;
// false if that request failed or was a clear. pixels must hold ART_BYTES(size).
bool art_get(char *url_out, size_t n, uint8_t *pixels);
