// Bluetooth fallback (firmware milestone 8): a BLE HID media remote.
// One consumer-control report: play/pause, next, previous, volume up/down.
// Works with any phone; bonds are kept in NVS (NimBLE store).
// Wi-Fi and Bluetooth are exclusive modes, chosen at boot (see app_main.c).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    BTR_OFF,
    BTR_ADVERTISING,        // waiting for a phone (new pairing or a bonded one coming back)
    BTR_CONNECTED,          // link up, not yet encrypted
    BTR_READY,              // encrypted: key presses reach the phone
} btr_state_t;

// HID consumer usages (USB HID Usage Tables, page 0x0C)
#define BTR_PLAY_PAUSE 0x00CD
#define BTR_NEXT       0x00B5
#define BTR_PREV       0x00B6
#define BTR_VOL_UP     0x00E9
#define BTR_VOL_DOWN   0x00EA

esp_err_t btremote_start(const char *name);
btr_state_t btremote_state(void);
const char *btremote_state_name(btr_state_t s);
uint32_t btremote_version(void);     // bumps on every state change
int btremote_bond_count(void);

// Press and release one consumer key. ESP_ERR_INVALID_STATE unless READY.
esp_err_t btremote_key(uint16_t usage);

// Battery level for the phone's Bluetooth battery widget (0..100). HOGP
// requires the Battery Service, so report the real level rather than a stale one.
void btremote_set_battery(int pct);

// Delete all bonds (the phone must "Forget This Device" too) and drop the link.
void btremote_forget(void);

// Release Bluetooth RAM for good (Wi-Fi mode). Bluetooth needs a restart after this.
void btremote_release_memory(void);
