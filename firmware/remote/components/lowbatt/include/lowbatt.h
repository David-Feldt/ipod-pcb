// Low battery -> deep sleep (firmware milestone 7, minimal version).
// Requirements: FIRMWARE-NOTES.md "Low-battery detection and sleep".
//
// Running on battery, VBAT below CONFIG_REMOTE_LOW_BATT_MV for several
// readings in a row stops Wi-Fi, shows a "Battery low" screen and enters deep
// sleep. USB insertion or a front button wakes it; a wake with the battery
// still below the threshold plus CONFIG_REMOTE_LOW_BATT_HYST_MV, and no USB,
// goes straight back to sleep without touching Wi-Fi or the display.
// With USB present the board keeps running (accepted prototype policy).
#pragma once
#include <stdbool.h>

// Call after power_init() and the 250 ms VBAT settle, before ui_init().
// Does not return when a low-battery wake should sleep again. Returns true
// when the battery is already low at a fresh power-on: show the screen with
// lowbatt_show_screen() and call lowbatt_shutdown().
bool lowbatt_boot_check(void);

// Call every ~2 s from the main loop.
void lowbatt_poll(void);

// Load the "Battery low" screen (caller may hold ui_lock()).
void lowbatt_show_screen(void);

// Stop Wi-Fi, show the screen (unless the battery is critically low), sleep
// the panel and enter deep sleep. Must not be called with ui_lock() held.
void lowbatt_shutdown(const char *why) __attribute__((noreturn));

int lowbatt_threshold_mv(void);
int lowbatt_resume_mv(void);
