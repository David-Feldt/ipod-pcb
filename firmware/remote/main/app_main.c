// E-ink Remote application entry point.
// Milestone 2: e-paper + LVGL now-playing screen driven by mock data, so the
// display, refresh policy and controls can be judged before Spotify exists.
//   PLAY       toggle play/pause          PREV/NEXT  previous/next mock track (full refresh)
//   wheel      volume overlay             SEL long   force a full refresh
#include "board.h"
#include "settings.h"
#include "app_console.h"
#include "ui.h"
#include "epd.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "app";

typedef struct { const char *title, *artist, *album; int duration_ms; } mock_track_t;
static const mock_track_t s_tracks[] = {
    { "Everything In Its Right Place", "Radiohead", "Kid A", 251000 },
    { "Midnight City", "M83", "Hurry Up, We're Dreaming", 243000 },
    { "Dreams", "Fleetwood Mac", "Rumours", 257000 },
    { "A Really Quite Long Song Title To Test Wrapping And Truncation", "Some Artist feat. Another Artist",
      "An Album Name That Is Also Long", 61000 },
};
#define N_TRACKS ((int)(sizeof s_tracks / sizeof s_tracks[0]))

static int s_track;
static bool s_playing = true;
static int64_t s_pos_ms;            // playback position at s_pos_at_us
static int64_t s_pos_at_us;

static int64_t position_ms(void)
{
    int64_t p = s_pos_ms;
    if (s_playing) p += (esp_timer_get_time() - s_pos_at_us) / 1000;
    return p;
}

static void load_track(int i)
{
    s_track = (i + N_TRACKS) % N_TRACKS;
    s_pos_ms = 0;
    s_pos_at_us = esp_timer_get_time();
    const mock_track_t *t = &s_tracks[s_track];
    ui_lock();
    np_set_track(t->title, t->artist, t->album, t->duration_ms);
    ui_unlock();
    ui_request_full_refresh();      // track change clears ghosting
}

static void update_status(void)
{
    int mv = power_vbat_mv();
    ui_status_t st = { .battery_pct = power_battery_pct(mv), .usb = power_usb_present(), .wifi = false };
    ui_lock();
    np_set_status(&st);
    ui_unlock();
}

static void load_calibration(void)
{
    int32_t mn, mx;
    if (settings_get_i32(SETTING_WHEEL_MIN_MV, &mn) == ESP_OK &&
        settings_get_i32(SETTING_WHEEL_MAX_MV, &mx) == ESP_OK) {
        wheel_set_calibration((wheel_cal_t){ .min_mv = mn, .max_mv = mx });
    }
}

static ui_insets_t load_insets(void)
{
    int32_t v[4] = { 0 };
    const char *keys[] = { SETTING_INSET_LEFT, SETTING_INSET_RIGHT, SETTING_INSET_TOP, SETTING_INSET_BOTTOM };
    for (int i = 0; i < 4; i++) {
        settings_get_i32(keys[i], &v[i]);
        if (v[i] < 0 || v[i] > UI_INSET_MAX) v[i] = 0;
    }
    return (ui_insets_t){ .left = v[0], .right = v[1], .top = v[2], .bottom = v[3] };
}

static void handle_input(const input_event_t *ev)
{
    if (ev->type == INPUT_WHEEL) {
        ESP_LOGI(TAG, "wheel %d%%", ev->value);
        ui_lock(); np_show_volume(ev->value); ui_unlock();
        return;
    }
    if (ev->type == INPUT_BTN_LONG && ev->button == BTN_SEL) {
        ESP_LOGI(TAG, "manual full refresh");
        ui_request_full_refresh();
        return;
    }
    if (ev->type != INPUT_BTN_PRESS) return;
    ESP_LOGI(TAG, "%s", input_button_name(ev->button));
    switch (ev->button) {
    case BTN_PLAY:
        s_pos_ms = position_ms();
        s_pos_at_us = esp_timer_get_time();
        s_playing = !s_playing;
        ui_lock(); np_set_playing(s_playing); ui_unlock();
        break;
    case BTN_NEXT: load_track(s_track + 1); break;
    case BTN_PREV:
        // Like most players: restart the track unless we're near its start.
        if (position_ms() > 3000) { s_pos_ms = 0; s_pos_at_us = esp_timer_get_time(); }
        else load_track(s_track - 1);
        break;
    default: break;
    }
}

void app_main(void)
{
    // Charger pins first so EN1/EN2 are driven LOW (USB100) as early as possible.
    ESP_ERROR_CHECK(power_init());
    ESP_ERROR_CHECK(settings_init());
    load_calibration();

    // VBAT divider/filter needs >= 250 ms to settle after power-on.
    vTaskDelay(pdMS_TO_TICKS(250));
    power_apply_usb_policy();
    ESP_LOGI(TAG, "vbat %d mV, usb %s, charger %s", power_vbat_mv(),
             power_usb_present() ? "present" : "absent", power_charger_mode_name(power_charger_mode()));

    QueueHandle_t events = xQueueCreate(16, sizeof(input_event_t));
    ESP_ERROR_CHECK(input_start(events));
    ESP_ERROR_CHECK(ui_init());
    // Default orientation: rotated 180 deg relative to the panel's native scan
    // (user report, board 1). Override with `display orient`.
    int32_t orient = EPD_ORIENT_ROT180;
    settings_get_i32(SETTING_DISPLAY_ORIENT, &orient);
    epd_set_orientation((epd_orient_t)orient);
    ui_insets_t insets = load_insets();
    ESP_LOGI(TAG, "screen insets L%d R%d T%d B%d", insets.left, insets.right, insets.top, insets.bottom);
    ui_lock();
    np_create(&insets);
    np_set_playing(s_playing);
    ui_unlock();
    load_track(0);
    update_status();
    ESP_ERROR_CHECK(app_console_start());

    TickType_t last_power = xTaskGetTickCount();
    TickType_t last_progress = xTaskGetTickCount();
    for (;;) {
        input_event_t ev;
        if (xQueueReceive(events, &ev, pdMS_TO_TICKS(100)) == pdTRUE) handle_input(&ev);

        TickType_t now = xTaskGetTickCount();
        if (now - last_progress >= pdMS_TO_TICKS(CONFIG_REMOTE_PROGRESS_INTERVAL_S * 1000)) {
            last_progress = now;
            int64_t p = position_ms();
            if (p >= s_tracks[s_track].duration_ms) load_track(s_track + 1);   // auto-advance
            else { ui_lock(); np_set_progress((int)p); ui_unlock(); }
        }
        if (now - last_power >= pdMS_TO_TICKS(2000)) {
            last_power = now;
            power_apply_usb_policy();      // handles USB plug/unplug
            static int n;
            if (++n % 15 == 1) update_status();   // battery text every ~30 s
        }
    }
}
