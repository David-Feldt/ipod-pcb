// E-ink Remote application entry point.
// Milestone 4: the now-playing screen shows live Spotify playback.
//   SEL (centre)  play/pause      NEXT  next track
//   PREV  restart, or previous track within the first 3 s
//   wheel volume of the playing device
//   PLAY (bottom) unassigned; hold it to force a full refresh
//   MENU (top)    open the menu
// In the menu: MENU / PLAY move up / down, SEL chooses, PREV goes back.
// On the sign-in and Wi-Fi setup screens MENU cancels.
// Button names follow the board's silkscreen (iPod cross); the user chose
// play/pause on the centre button.
// The player task does all Spotify I/O; this loop maps its state onto the UI.
#include <string.h>
#include "board.h"
#include "settings.h"
#include "app_console.h"
#include "ui.h"
#include "epd.h"
#include "net.h"
#include "spotify.h"
#include "player.h"
#include "art.h"
#include "pairing.h"
#include "wifisetup.h"
#include "lowbatt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "app";

// What the screen currently shows, so we only touch LVGL on real changes
// (every LVGL change costs an e-paper refresh).
static struct {
    char track_id[64];
    char message[96];
    int playing;            // -1 unknown
    char art_url[128];      // cover currently on screen ("" = placeholder)
} s_shown = { .playing = -1 };

// A new track waits briefly for its cover so text and art arrive in one full
// refresh instead of two.
#define ART_WAIT_MS 2500
static struct {
    bool active;
    int64_t deadline_us;
    player_state_t st;
} s_pending;
static uint8_t s_art_buf[ART_BYTES(UI_ART_MAX)];

static void update_status(void)
{
    int mv = power_vbat_mv();
    ui_status_t st = { .battery_pct = power_battery_pct(mv), .usb = power_usb_present(), .wifi = net_connected() };
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

static void show_message(const char *msg)
{
    if (!strcmp(s_shown.message, msg ? msg : "")) return;
    snprintf(s_shown.message, sizeof s_shown.message, "%s", msg ? msg : "");
    ui_lock(); np_show_message(msg); ui_unlock();
}

static const char *status_message(const player_state_t *s)
{
    switch (s->status) {
    case PLAYER_STARTING: return "Connecting to Spotify...";
    case PLAYER_IDLE:     return "Nothing playing. Start Spotify on a phone or speaker";
    case PLAYER_NO_WIFI:
        if (net_state() == NET_NO_CREDENTIALS) return "No Wi-Fi set up. Press MENU to set it up";
        if (net_state() == NET_DISCONNECTED) return "Can't reach Wi-Fi. Retrying (MENU to change it)";
        return "Connecting to Wi-Fi...";
    case PLAYER_NO_TOKEN: return "Not signed in. Press MENU to sign in";
    case PLAYER_REJECTED: return "Spotify sign-in expired. Press MENU to sign in";
    case PLAYER_ERROR:    return s_shown.track_id[0] ? NULL : "Can't reach Spotify. Retrying";
    default:              return NULL;
    }
}

static void apply_player_state(void)
{
    player_state_t s;
    player_get(&s);

    char notice[64];
    player_take_notice(notice, sizeof notice);
    if (notice[0]) { ui_lock(); np_show_notice(notice); ui_unlock(); }

    const char *msg = status_message(&s);
    if (msg) { show_message(msg); return; }
    if (s.status == PLAYER_ERROR) return;       // transient: keep showing the last track
    show_message(NULL);

    if (strcmp(s.track_id, s_shown.track_id)) {
        if (!s_pending.active || strcmp(s.track_id, s_pending.st.track_id)) {
            s_pending.active = true;
            s_pending.deadline_us = esp_timer_get_time() + ART_WAIT_MS * 1000LL;
            art_request(s.image_url);
        }
        s_pending.st = s;
        return;                                 // shown by show_pending_track()
    }

    ui_lock();
    if (s.playing != s_shown.playing) {
        s_shown.playing = s.playing;
        np_set_playing(s.playing);
    }
    np_set_progress(player_position_ms(&s));    // redraws only if a pixel changes
    ui_unlock();
}

// Put the cover on screen if the art task has it. Returns true if it changed.
static bool apply_art(const char *want_url)
{
    char url[128];
    bool ok = art_get(url, sizeof url, s_art_buf);
    if (strcmp(url, want_url)) return false;           // not finished (or stale)
    if (!strcmp(s_shown.art_url, ok ? url : "")) return false;
    snprintf(s_shown.art_url, sizeof s_shown.art_url, "%s", ok ? url : "");
    np_set_art(ok ? s_art_buf : NULL);
    return true;
}

static void show_pending_track(void)
{
    if (!s_pending.active) return;
    const player_state_t *s = &s_pending.st;
    char url[128];
    art_get(url, sizeof url, s_art_buf);
    bool art_ready = !s->image_url[0] || !strcmp(url, s->image_url) || !strcmp(s_shown.art_url, s->image_url);
    if (!art_ready && esp_timer_get_time() < s_pending.deadline_us) return;
    s_pending.active = false;

    ui_lock();
    snprintf(s_shown.track_id, sizeof s_shown.track_id, "%s", s->track_id);
    ESP_LOGI(TAG, "track: %s - %s%s", s->title, s->artist, art_ready ? "" : " (cover late)");
    np_set_track(s->title, s->artist, s->album, s->duration_ms);
    if (!apply_art(s->image_url) && strcmp(s_shown.art_url, s->image_url)) {
        s_shown.art_url[0] = 0;                 // old cover would be misleading
        np_set_art(NULL);
    }
    if (s->playing != s_shown.playing) { s_shown.playing = s->playing; np_set_playing(s->playing); }
    np_set_progress(player_position_ms(s));
    ui_unlock();
    ui_request_full_refresh();                  // track change clears ghosting
}

// Mirror the pairing state on screen; back to now playing when it ends.
static void apply_pairing(void)
{
    static bool shown;
    pairing_info_t p;
    pairing_get(&p);
    if (p.state == PAIR_OFF) {
        if (shown) {
            shown = false;
            ui_lock(); np_show(); ui_unlock();
            ui_request_full_refresh();
        }
        return;
    }
    shown = true;
    char qr[32];
    snprintf(qr, sizeof qr, "http://%s/", p.ip);
    ui_lock();
    switch (p.state) {
    case PAIR_WAITING:
        pair_screen_show("Sign in to Spotify", qr, p.ip,
                         p.error[0] ? p.error : "Scan, or open this address on a phone on the same Wi-Fi",
                         "MENU: cancel");
        break;
    case PAIR_SIGNING_IN:
        pair_screen_show("Sign in to Spotify", NULL, "Signing in...", NULL, NULL);
        break;
    default:
        pair_screen_show("Sign in to Spotify", NULL, "Signed in", NULL, NULL);
        break;
    }
    ui_unlock();
    if (p.state == PAIR_DONE) player_poll_soon();
}

// ------------------------------------------------------------------ menu
#define MENU_IDLE_MS 20000          // back to now playing when left alone

typedef enum { MI_SIGN_IN, MI_WIFI, MI_BACK, MI_COUNT } menu_item_t;
static struct {
    bool open;
    int sel;
    int64_t last_us;
} s_menu;

static void menu_render(void)
{
    static const char *const items[MI_COUNT] = { [MI_SIGN_IN] = "Sign in to Spotify", [MI_WIFI] = "Set up Wi-Fi",
                                                  [MI_BACK] = "Back" };
    ui_lock(); menu_screen_show(items, MI_COUNT, s_menu.sel); ui_unlock();
}

static void menu_open(void)
{
    s_menu.open = true;
    s_menu.sel = 0;
    s_menu.last_us = esp_timer_get_time();
    menu_render();
}

static void menu_close(void)
{
    if (!s_menu.open) return;
    s_menu.open = false;
    ui_lock(); np_show(); ui_unlock();
    ui_request_full_refresh();
}

static void start_pairing(void)
{
    s_menu.open = false;                // the sign-in screen replaces the menu
    wifisetup_stop();                   // both use port 80
    esp_err_t err = pairing_start();
    if (err != ESP_OK) { ui_lock(); np_show(); ui_unlock(); ui_request_full_refresh(); }
    if (err == ESP_ERR_INVALID_STATE) { ui_lock(); np_show_notice("Wi-Fi not connected"); ui_unlock(); }
    else if (err != ESP_OK) ESP_LOGE(TAG, "pairing start failed: %s", esp_err_to_name(err));
}

static void start_wifisetup(void)
{
    s_menu.open = false;
    pairing_stop();
    esp_err_t err = wifisetup_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi setup failed to start: %s", esp_err_to_name(err));
        ui_lock(); np_show(); np_show_notice("Wi-Fi setup failed"); ui_unlock();
        ui_request_full_refresh();
    }
}

static bool spotify_needs_login(void)
{
    sp_auth_t a = spotify_auth_state();
    return a == SP_AUTH_NO_TOKEN || a == SP_AUTH_REJECTED;
}

// Mirror the Wi-Fi setup state on screen. After a successful setup, go
// straight on to Spotify sign-in if there is no login yet.
static void apply_wifisetup(void)
{
    static bool shown, joined;
    wifisetup_info_t w;
    wifisetup_get(&w);
    if (w.state == WS_OFF) {
        if (!shown) return;
        shown = false;
        if (joined && spotify_needs_login()) {
            joined = false;
            start_pairing();                    // its screen replaces this one
            return;
        }
        joined = false;
        ui_lock(); np_show(); ui_unlock();
        ui_request_full_refresh();
        return;
    }
    shown = true;
    char qr[80], line[128];
    ui_lock();
    switch (w.state) {
    case WS_WAITING:
        wifisetup_join_qr(&w, qr, sizeof qr);
        snprintf(line, sizeof line, "%s%sPassword %s", w.error, w.error[0] ? "\n" : "", w.ap_pass);
        pair_screen_show("Set up Wi-Fi", qr, w.ap_ssid, line, "Scan to join. MENU: cancel");
        break;
    case WS_CONNECTING:
        snprintf(line, sizeof line, "Connecting to %s...", w.target);
        pair_screen_show("Set up Wi-Fi", NULL, line, NULL, NULL);
        break;
    default:
        joined = true;
        pair_screen_show("Set up Wi-Fi", NULL, "Connected", w.target, NULL);
        break;
    }
    ui_unlock();
}

// First boot without a login: offer the sign-in screen once Wi-Fi is up.
static void maybe_auto_pair(void)
{
    static bool done;
    if (done || !net_connected() || wifisetup_active()) return;
    player_state_t s;
    player_get(&s);
    if (s.status != PLAYER_NO_TOKEN && s.status != PLAYER_REJECTED) return;
    done = true;
    ESP_LOGI(TAG, "no Spotify login: starting pairing");
    start_pairing();
}

static void menu_input(button_id_t b)
{
    s_menu.last_us = esp_timer_get_time();
    switch (b) {
    case BTN_MENU: s_menu.sel = (s_menu.sel + MI_COUNT - 1) % MI_COUNT; menu_render(); break;
    case BTN_PLAY: s_menu.sel = (s_menu.sel + 1) % MI_COUNT; menu_render(); break;
    case BTN_PREV: menu_close(); break;
    case BTN_SEL:
        if (s_menu.sel == MI_SIGN_IN) start_pairing();
        else if (s_menu.sel == MI_WIFI) start_wifisetup();
        else menu_close();
        break;
    default: break;
    }
}

static void handle_input(const input_event_t *ev)
{
    if (ev->type == INPUT_WHEEL) {
        ESP_LOGI(TAG, "wheel %d%%", ev->value);
        ui_lock(); np_show_volume(ev->value); ui_unlock();
        player_set_volume(ev->value);
        return;
    }
    if (ev->type == INPUT_BTN_LONG && ev->button == BTN_PLAY) {
        ESP_LOGI(TAG, "manual full refresh");
        ui_request_full_refresh();
        return;
    }
    if (ev->type != INPUT_BTN_PRESS) return;
    ESP_LOGI(TAG, "%s", input_button_name(ev->button));
    if (s_menu.open) { menu_input(ev->button); return; }
    if (ev->button == BTN_MENU) {
        if (pairing_active()) pairing_stop();
        else if (wifisetup_active()) wifisetup_stop();
        else menu_open();
        return;
    }
    switch (ev->button) {
    case BTN_SEL: player_toggle(); break;
    case BTN_NEXT: player_next(); break;
    case BTN_PREV: player_prev(); break;
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
    bool low_at_boot = lowbatt_boot_check();    // a low-battery wake may go straight back to sleep here

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
    pair_screen_create(&insets);
    menu_screen_create(&insets);
    if (low_at_boot) lowbatt_show_screen();     // before the first frame, so now playing never draws
    ui_unlock();
    if (low_at_boot) lowbatt_shutdown("battery low at power-on");
    update_status();
    ESP_ERROR_CHECK(spotify_init());
    ESP_ERROR_CHECK(app_console_start());
    ESP_ERROR_CHECK(net_start());
    ESP_ERROR_CHECK(player_start());
    ESP_ERROR_CHECK(art_start(np_art_size()));
    apply_player_state();                  // "Connecting..." until the first poll answers

    TickType_t last_power = xTaskGetTickCount();
    TickType_t last_progress = xTaskGetTickCount();
    uint32_t seen_version = player_version();
    uint32_t seen_art = art_version();
    uint32_t seen_pair = pairing_version();
    uint32_t seen_wifi = wifisetup_version();
    if (!net_have_credentials()) {
        ESP_LOGI(TAG, "no Wi-Fi saved: starting the setup hotspot");
        start_wifisetup();
    }
    for (;;) {
        input_event_t ev;
        if (xQueueReceive(events, &ev, pdMS_TO_TICKS(100)) == pdTRUE) handle_input(&ev);

        TickType_t now = xTaskGetTickCount();
        uint32_t v = player_version();
        if (v != seen_version) {
            seen_version = v;
            apply_player_state();
        } else if (now - last_progress >= pdMS_TO_TICKS(CONFIG_REMOTE_PROGRESS_INTERVAL_S * 1000)) {
            last_progress = now;
            apply_player_state();              // advances the progress bar between polls
        }
        show_pending_track();
        uint32_t pv = pairing_version();
        if (pv != seen_pair) {
            seen_pair = pv;
            apply_pairing();
        }
        uint32_t wv = wifisetup_version();
        if (wv != seen_wifi) {
            seen_wifi = wv;
            apply_wifisetup();
        }
        maybe_auto_pair();
        if (s_menu.open && esp_timer_get_time() - s_menu.last_us > MENU_IDLE_MS * 1000LL) menu_close();
        uint32_t av = art_version();
        if (av != seen_art) {
            seen_art = av;
            // A cover that missed the track-change refresh lands with a partial.
            if (!s_pending.active && s_shown.track_id[0]) {
                player_state_t s;
                player_get(&s);
                if (!strcmp(s.track_id, s_shown.track_id)) { ui_lock(); apply_art(s.image_url); ui_unlock(); }
            }
        }
        if (now - last_power >= pdMS_TO_TICKS(2000)) {
            last_power = now;
            power_apply_usb_policy();      // handles USB plug/unplug
            lowbatt_poll();                // may enter deep sleep
            static int n;
            static bool was_connected;
            bool conn = net_connected();
            if (++n % 15 == 1 || conn != was_connected) update_status();   // battery every ~30 s, Wi-Fi on change
            was_connected = conn;
        }
    }
}
