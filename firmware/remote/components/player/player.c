#include <string.h>
#include <stdio.h>
#include "player.h"
#include "spotify.h"
#include "net.h"
#include "cJSON.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

static const char *TAG = "player";

#define POLL_PLAYING_MS   2000
#define POLL_IDLE_MS      10000
#define POLL_AFTER_CMD_MS 700     // Spotify takes a moment to reflect a command
#define OPTIMISTIC_MS     2500    // keep our play/pause guess over stale polls
#define VOLUME_GAP_MS     300
#define RETRY_ERROR_MS    5000
#define RETRY_AUTH_MS     30000

typedef enum { CMD_TOGGLE, CMD_NEXT, CMD_PREV, CMD_VOLUME } cmd_t;

static SemaphoreHandle_t s_lock;
static QueueHandle_t s_cmds;
static TaskHandle_t s_task;
static player_state_t s_st = { .status = PLAYER_STARTING, .volume = -1, .volume_allowed = true };
static volatile uint32_t s_version;
static char s_notice[64];
static int64_t s_optimistic_until;   // esp_timer us
static volatile int s_pending_volume = -1;

static int64_t now_us(void) { return esp_timer_get_time(); }

static void bump(void) { s_version++; }

static void set_status(player_status_t st)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_st.status != st) { s_st.status = st; bump(); }
    xSemaphoreGive(s_lock);
}

static void notice(const char *msg)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(s_notice, sizeof s_notice, "%s", msg);
    bump();
    xSemaphoreGive(s_lock);
}

static void copy_str(char *dst, size_t n, cJSON *o, const char *key)
{
    const char *v = cJSON_GetStringValue(cJSON_GetObjectItem(o, key));
    snprintf(dst, n, "%s", v ? v : "");
}

// Join artist names: "A, B".
static void artists(char *dst, size_t n, cJSON *arr)
{
    dst[0] = 0;
    cJSON *a;
    cJSON_ArrayForEach(a, arr) {
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(a, "name"));
        if (!name) continue;
        size_t len = strlen(dst);
        snprintf(dst + len, n - len, "%s%s", len ? ", " : "", name);
    }
}

// Smallest image that is still >= 64 px (Spotify lists largest first).
static void pick_image(char *dst, size_t n, cJSON *images)
{
    dst[0] = 0;
    cJSON *img;
    cJSON_ArrayForEach(img, images) {
        cJSON *w = cJSON_GetObjectItem(img, "width");
        const char *url = cJSON_GetStringValue(cJSON_GetObjectItem(img, "url"));
        if (url && (!cJSON_IsNumber(w) || w->valueint >= 64 || !dst[0])) snprintf(dst, n, "%s", url);
    }
}

static void parse_player(const char *body, int64_t t_req)
{
    cJSON *j = cJSON_Parse(body);
    if (!j) { ESP_LOGW(TAG, "unparseable player response"); return; }
    cJSON *item = cJSON_GetObjectItem(j, "item"), *dev = cJSON_GetObjectItem(j, "device");

    player_state_t n;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    n = s_st;
    xSemaphoreGive(s_lock);

    n.status = PLAYER_OK;
    bool playing = cJSON_IsTrue(cJSON_GetObjectItem(j, "is_playing"));
    if (now_us() >= s_optimistic_until) n.playing = playing;
    cJSON *prog = cJSON_GetObjectItem(j, "progress_ms");
    n.progress_ms = cJSON_IsNumber(prog) ? prog->valueint : 0;
    n.progress_at_us = t_req;          // conservative: position as of when we asked
    copy_str(n.device, sizeof n.device, dev, "name");
    cJSON *vol = cJSON_GetObjectItem(dev, "volume_percent");
    n.volume = cJSON_IsNumber(vol) ? vol->valueint : -1;
    cJSON *sv = cJSON_GetObjectItem(dev, "supports_volume");
    n.volume_allowed = !cJSON_IsFalse(sv);

    if (cJSON_IsObject(item)) {
        copy_str(n.track_id, sizeof n.track_id, item, "id");
        if (!n.track_id[0]) copy_str(n.track_id, sizeof n.track_id, item, "uri");   // local files
        copy_str(n.title, sizeof n.title, item, "name");
        cJSON *dur = cJSON_GetObjectItem(item, "duration_ms");
        n.duration_ms = cJSON_IsNumber(dur) ? dur->valueint : 0;
        cJSON *album = cJSON_GetObjectItem(item, "album");
        if (album) {                                   // track
            artists(n.artist, sizeof n.artist, cJSON_GetObjectItem(item, "artists"));
            copy_str(n.album, sizeof n.album, album, "name");
            pick_image(n.image_url, sizeof n.image_url, cJSON_GetObjectItem(album, "images"));
        } else {                                       // podcast episode
            cJSON *show = cJSON_GetObjectItem(item, "show");
            copy_str(n.artist, sizeof n.artist, show, "name");
            copy_str(n.album, sizeof n.album, show, "publisher");
            pick_image(n.image_url, sizeof n.image_url, cJSON_GetObjectItem(item, "images"));
        }
    } else {
        // Active device but no item (ad, or between tracks): keep the last track.
        if (!n.track_id[0]) snprintf(n.title, sizeof n.title, "%s", playing ? "Advertisement" : "");
    }
    cJSON_Delete(j);

    // Position moves on every poll; only treat it as a change (which costs an
    // e-paper refresh) when it jumps, e.g. after a seek on another device.
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int drift = player_position_ms(&n) - player_position_ms(&s_st);
    player_state_t a = n, b = s_st;
    a.progress_ms = b.progress_ms = 0;
    a.progress_at_us = b.progress_at_us = 0;
    bool changed = memcmp(&a, &b, sizeof a) || drift > 3000 || drift < -3000;
    s_st = n;
    if (changed) bump();
    xSemaphoreGive(s_lock);
}

// Returns false on a hard error (no response).
static bool poll(void)
{
    sp_response_t r;
    int64_t t0 = now_us();
    esp_err_t err = spotify_request("GET", "/v1/me/player?additional_types=episode", NULL, &r);
    bool ok = err == ESP_OK;
    if (!ok) {
        sp_auth_t a = spotify_auth_state();
        set_status(a == SP_AUTH_NO_TOKEN ? PLAYER_NO_TOKEN : a == SP_AUTH_REJECTED ? PLAYER_REJECTED : PLAYER_ERROR);
    } else if (r.status == 200) {
        parse_player(r.body, t0);
    } else if (r.status == 204) {
        set_status(PLAYER_IDLE);
    } else if (r.status == 429) {
        ESP_LOGW(TAG, "rate limited; retry after %d s", r.retry_after_s);
        vTaskDelay(pdMS_TO_TICKS((r.retry_after_s > 0 ? r.retry_after_s : 5) * 1000));
    } else {
        ESP_LOGW(TAG, "player HTTP %d: %.120s", r.status, r.body);
        set_status(PLAYER_ERROR);
        ok = false;
    }
    spotify_response_free(&r);
    return ok;
}

// Map a failed command response to something worth saying on screen.
static void command_failed(const char *what, const sp_response_t *r)
{
    ESP_LOGW(TAG, "%s: HTTP %d %.160s", what, r->status, r->body ? r->body : "");
    const char *b = r->body ? r->body : "";
    if (strstr(b, "VOLUME_CONTROL_DISALLOW")) notice("Volume not supported");
    else if (strstr(b, "PREMIUM_REQUIRED")) notice("Premium required");
    else if (r->status == 404) notice("No active device");
    else if (r->status == 403) notice("Not allowed now");
    else if (r->status == 429) notice("Spotify busy");
    else notice("Command failed");
}

static void run(const char *what, const char *method, const char *path)
{
    sp_response_t r;
    esp_err_t err = spotify_request(method, path, NULL, &r);
    if (err != ESP_OK) notice("No connection");
    else if (r.status < 200 || r.status >= 300) command_failed(what, &r);
    else ESP_LOGI(TAG, "%s ok (HTTP %d)", what, r.status);
    spotify_response_free(&r);
}

static void do_command(cmd_t c)
{
    char path[64];
    switch (c) {
    case CMD_TOGGLE: {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool play = !s_st.playing;
        s_st.playing = play;                           // optimistic
        if (play) s_st.progress_at_us = now_us();       // position was frozen while paused
        else {
            s_st.progress_ms = player_position_ms(&s_st);
            s_st.progress_at_us = now_us();
        }
        s_optimistic_until = now_us() + OPTIMISTIC_MS * 1000LL;
        bump();
        xSemaphoreGive(s_lock);
        run(play ? "play" : "pause", "PUT", play ? "/v1/me/player/play" : "/v1/me/player/pause");
        break;
    }
    case CMD_NEXT: run("next", "POST", "/v1/me/player/next"); break;
    case CMD_PREV: {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        int pos = player_position_ms(&s_st);
        if (pos > 3000) { s_st.progress_ms = 0; s_st.progress_at_us = now_us(); bump(); }
        xSemaphoreGive(s_lock);
        if (pos > 3000) run("restart", "PUT", "/v1/me/player/seek?position_ms=0");
        else run("previous", "POST", "/v1/me/player/previous");
        break;
    }
    case CMD_VOLUME: {
        int v = s_pending_volume;
        s_pending_volume = -1;
        if (v < 0) break;
        snprintf(path, sizeof path, "/v1/me/player/volume?volume_percent=%d", v);
        run("volume", "PUT", path);
        break;
    }
    }
}

static void task(void *arg)
{
    int64_t next_poll = 0, last_volume = 0;
    for (;;) {
        // Work out how long we may sleep.
        int64_t t = now_us();
        int64_t wake = next_poll;
        if (s_pending_volume >= 0) {
            int64_t v_at = last_volume + VOLUME_GAP_MS * 1000LL;
            if (v_at < wake) wake = v_at;
        }
        TickType_t wait = wake > t ? pdMS_TO_TICKS((wake - t) / 1000) + 1 : 0;

        cmd_t c;
        bool got = xQueueReceive(s_cmds, &c, wait) == pdTRUE;

        if (!net_connected()) {
            set_status(PLAYER_NO_WIFI);
            net_wait_connected(portMAX_DELAY);
            xQueueReset(s_cmds);           // presses made while offline are stale
            s_pending_volume = -1;
            next_poll = 0;
            continue;
        }
        t = now_us();
        if (got && c != CMD_VOLUME) {
            do_command(c);
            next_poll = now_us() + POLL_AFTER_CMD_MS * 1000LL;
            continue;
        }
        if (s_pending_volume >= 0 && t >= last_volume + VOLUME_GAP_MS * 1000LL) {
            last_volume = t;
            do_command(CMD_VOLUME);
            continue;
        }
        if (t < next_poll) continue;

        bool ok = poll();
        player_state_t s;
        player_get(&s);
        int interval;
        if (s.status == PLAYER_NO_TOKEN || s.status == PLAYER_REJECTED) interval = RETRY_AUTH_MS;
        else if (!ok) interval = RETRY_ERROR_MS;
        else if (s.status == PLAYER_OK && s.playing) {
            interval = POLL_PLAYING_MS;
            // Poll right after the track should end so the next one shows promptly.
            int left = s.duration_ms - player_position_ms(&s);
            if (left > 0 && left + 300 < interval) interval = left + 300;
        } else interval = POLL_IDLE_MS;
        next_poll = now_us() + interval * 1000LL;
    }
}

static void send(cmd_t c)
{
    if (s_cmds && xQueueSend(s_cmds, &c, 0) != pdTRUE) ESP_LOGW(TAG, "command queue full");
}

void player_toggle(void) { send(CMD_TOGGLE); }
void player_next(void) { send(CMD_NEXT); }
void player_prev(void) { send(CMD_PREV); }

void player_set_volume(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_pending_volume = pct;
    if (s_task) xTaskAbortDelay(s_task);   // wake the task to schedule the send
}

uint32_t player_version(void) { return s_version; }

void player_get(player_state_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_st;
    xSemaphoreGive(s_lock);
}

int player_position_ms(const player_state_t *s)
{
    int64_t p = s->progress_ms;
    if (s->playing) p += (now_us() - s->progress_at_us) / 1000;
    if (s->duration_ms > 0 && p > s->duration_ms) p = s->duration_ms;
    return (int)p;
}

void player_take_notice(char *out, size_t n)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(out, n, "%s", s_notice);
    s_notice[0] = 0;
    xSemaphoreGive(s_lock);
}

const char *player_status_name(player_status_t s)
{
    static const char *n[] = { "starting", "ok", "idle", "no wifi", "no token", "rejected", "error" };
    return s <= PLAYER_ERROR ? n[s] : "?";
}

esp_err_t player_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_cmds = xQueueCreate(8, sizeof(cmd_t));
    if (!s_lock || !s_cmds) return ESP_ERR_NO_MEM;
    return xTaskCreate(task, "player", 8192, NULL, 5, &s_task) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
