#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "pairing.h"
#include "net.h"
#include "spotify.h"
#include "esp_http_server.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "psa/crypto.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "pairing";

#define DEV_REDIRECT_URI "http://127.0.0.1:8888/callback"
#define SCOPES           "user-read-playback-state%%20user-modify-playback-state"   // printf format
#define AUTH_VALID_US    (10 * 60 * 1000000LL)    // a started sign-in expires after 10 min
#define DONE_LINGER_US   (5 * 1000000LL)          // keep "Signed in" up briefly, then stop
#define CODE_MAX         1024

static SemaphoreHandle_t s_lock;
static httpd_handle_t s_server;
static esp_timer_handle_t s_stop_timer;
static volatile uint32_t s_version;
static pairing_info_t s_info;

// The one sign-in in flight (started by /login, consumed by /callback).
static char s_verifier[72];
static char s_state[52];            // 32 hex chars, '-', device IPv4
static int64_t s_auth_at;

const char *pairing_redirect_uri(void)
{
    return CONFIG_REMOTE_PAIRING_REDIRECT_URI[0] ? CONFIG_REMOTE_PAIRING_REDIRECT_URI : DEV_REDIRECT_URI;
}

static void bump(void) { s_version++; }

static void set_state(pair_state_t st, const char *error)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_info.state = st;
    snprintf(s_info.error, sizeof s_info.error, "%s", error ? error : "");
    bump();
    xSemaphoreGive(s_lock);
}

static void arm_stop(int64_t us)
{
    esp_timer_stop(s_stop_timer);
    esp_timer_start_once(s_stop_timer, us);
}

// ------------------------------------------------------------------ PKCE

static void base64url(char *out, const uint8_t *in, size_t n)
{
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t o = 0, i = 0;
    for (; i + 2 < n; i += 3) {
        uint32_t v = in[i] << 16 | in[i + 1] << 8 | in[i + 2];
        out[o++] = t[v >> 18]; out[o++] = t[(v >> 12) & 63]; out[o++] = t[(v >> 6) & 63]; out[o++] = t[v & 63];
    }
    if (n - i == 1) {
        uint32_t v = in[i] << 16;
        out[o++] = t[v >> 18]; out[o++] = t[(v >> 12) & 63];
    } else if (n - i == 2) {
        uint32_t v = in[i] << 16 | in[i + 1] << 8;
        out[o++] = t[v >> 18]; out[o++] = t[(v >> 12) & 63]; out[o++] = t[(v >> 6) & 63];
    }
    out[o] = 0;                     // no '=' padding (RFC 7636)
}

// New verifier, its S256 challenge and a fresh state. Caller holds s_lock.
static esp_err_t new_authorization(const char *ip, char *challenge /* >= 44 */)
{
    uint8_t rnd[48], hash[32];
    esp_fill_random(rnd, sizeof rnd);
    base64url(s_verifier, rnd, sizeof rnd);             // 64 chars of [A-Za-z0-9-_]
    size_t hl = 0;
    if (psa_crypto_init() != PSA_SUCCESS ||
        psa_hash_compute(PSA_ALG_SHA_256, (const uint8_t *)s_verifier, strlen(s_verifier), hash, sizeof hash,
                         &hl) != PSA_SUCCESS || hl != sizeof hash) {
        s_verifier[0] = 0;
        return ESP_FAIL;
    }
    base64url(challenge, hash, sizeof hash);
    esp_fill_random(rnd, 16);
    int n = 0;
    for (int i = 0; i < 16; i++) n += sprintf(s_state + n, "%02x", rnd[i]);
    snprintf(s_state + n, sizeof s_state - n, "-%s", ip);
    s_auth_at = esp_timer_get_time();
    return ESP_OK;
}

// ------------------------------------------------------------------ pages

static void send_page(httpd_req_t *req, const char *body_html)
{
    static const char head[] =
        "<!doctype html><html><head><meta charset=utf-8>"
        "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
        "<title>E-ink Remote</title><style>"
        "body{font:17px/1.45 system-ui,sans-serif;max-width:26em;margin:2em auto;padding:0 1em;color:#111}"
        "h1{font-size:1.4em}.btn{display:inline-block;background:#1db954;color:#fff;font-weight:600;"
        "padding:.8em 1.4em;border-radius:2em;text-decoration:none}"
        ".err{background:#fde8e8;border-left:4px solid #c00;padding:.6em .8em}"
        ".ok{background:#e6f6ea;border-left:4px solid #1db954;padding:.6em .8em}"
        "small{color:#666}</style></head><body><h1>E-ink Remote</h1>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, head);
    httpd_resp_sendstr_chunk(req, body_html);
    httpd_resp_sendstr_chunk(req, "</body></html>");
    httpd_resp_sendstr_chunk(req, NULL);
}

static void send_status_page(httpd_req_t *req)
{
    pairing_info_t p;
    pairing_get(&p);
    char body[1024];
    if (p.state == PAIR_DONE) {
        snprintf(body, sizeof body,
                 "<p class=ok>Signed in to Spotify. The remote shows what's playing in a moment.</p>"
                 "<p>You can close this page.</p>");
    } else if (p.state == PAIR_SIGNING_IN) {
        snprintf(body, sizeof body, "<p>Signing in&hellip;</p><script>setTimeout(()=>location.reload(),2000)</script>");
    } else {
        snprintf(body, sizeof body,
                 "%s%s%s"
                 "<p>Connect this remote to your Spotify account.</p>"
                 "<p><a class=btn href=/login>Sign in with Spotify</a></p>"
                 "<p><small>Needs Spotify Premium, and your account must be on the remote's "
                 "list of allowed users. Signing in replaces any account already on the remote.</small></p>",
                 p.error[0] ? "<p class=err>" : "", p.error, p.error[0] ? "</p>" : "");
    }
    send_page(req, body);
}

static esp_err_t h_root(httpd_req_t *req)
{
    send_status_page(req);
    return ESP_OK;
}

static esp_err_t h_login(httpd_req_t *req)
{
    char ip[16], challenge[48], enc_redirect[3 * 256 + 1];
    net_ip_str(ip, sizeof ip);
    if (!ip[0]) {
        send_page(req, "<p class=err>The remote is not on Wi-Fi.</p>");
        return ESP_OK;
    }
    // URL-encode the redirect URI (only reserved characters matter here).
    const char *r = pairing_redirect_uri();
    size_t o = 0;
    for (; *r && o + 4 < sizeof enc_redirect; r++) {
        unsigned char ch = *r;
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || strchr("-_.~", ch)) {
            enc_redirect[o++] = ch;
        } else {
            o += sprintf(enc_redirect + o, "%%%02X", ch);
        }
    }
    enc_redirect[o] = 0;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = new_authorization(ip, challenge);
    char state[sizeof s_state];
    strcpy(state, s_state);
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) {
        send_page(req, "<p class=err>Could not start the sign-in (crypto error).</p>");
        return ESP_OK;
    }
    arm_stop((int64_t)PAIRING_TIMEOUT_MIN * 60 * 1000000);    // activity: restart the idle timeout

    char *url = malloc(2048);
    if (!url) return httpd_resp_send_500(req);
    snprintf(url, 2048,
             "https://accounts.spotify.com/authorize?client_id=%s&response_type=code&redirect_uri=%s"
             "&code_challenge_method=S256&code_challenge=%s&scope=" SCOPES "&state=%s&show_dialog=true",
             CONFIG_REMOTE_SPOTIFY_CLIENT_ID, enc_redirect, challenge, state);
    ESP_LOGI(TAG, "sign-in started; redirect URI %s", pairing_redirect_uri());
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", url);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, NULL, 0);
    free(url);
    return ESP_OK;
}

static void fail(httpd_req_t *req, const char *msg)
{
    ESP_LOGW(TAG, "sign-in failed: %s", msg);
    set_state(PAIR_WAITING, msg);
    send_status_page(req);
}

static esp_err_t h_callback(httpd_req_t *req)
{
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen == 0 || qlen > CODE_MAX + 200) { fail(req, "The sign-in link was incomplete. Try again."); return ESP_OK; }
    char *q = malloc(qlen + 1);
    char *code = malloc(CODE_MAX + 1);
    if (!q || !code) { free(q); free(code); return httpd_resp_send_500(req); }
    char state[sizeof s_state + 1] = "", error[40] = "";
    httpd_req_get_url_query_str(req, q, qlen + 1);
    if (httpd_query_key_value(q, "code", code, CODE_MAX + 1) != ESP_OK) code[0] = 0;
    httpd_query_key_value(q, "state", state, sizeof state);
    httpd_query_key_value(q, "error", error, sizeof error);
    free(q);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_info.state == PAIR_DONE) {            // reload after success: keep "signed in"
        xSemaphoreGive(s_lock);
        free(code);
        send_status_page(req);
        return ESP_OK;
    }
    bool fresh = s_verifier[0] && esp_timer_get_time() - s_auth_at < AUTH_VALID_US;
    bool match = fresh && !strcmp(state, s_state);
    char verifier[sizeof s_verifier];
    strcpy(verifier, s_verifier);
    if (match) s_verifier[0] = s_state[0] = 0;      // one use only
    xSemaphoreGive(s_lock);

    if (!match) {
        fail(req, fresh ? "That sign-in link doesn't belong to this remote. Try again."
                        : "That sign-in has expired. Try again.");
    } else if (error[0]) {
        fail(req, !strcmp(error, "access_denied") ? "Sign-in was cancelled." : "Spotify reported an error. Try again.");
    } else if (!code[0]) {
        fail(req, "Spotify sent no sign-in code. Try again.");
    } else {
        set_state(PAIR_SIGNING_IN, NULL);
        esp_err_t err = spotify_exchange_code(code, verifier, pairing_redirect_uri());
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "signed in");
            set_state(PAIR_DONE, NULL);
            arm_stop(DONE_LINGER_US);
            send_status_page(req);
        } else if (err == ESP_FAIL) {
            fail(req, "Spotify did not accept the sign-in. Try again.");
        } else {
            fail(req, "Couldn't reach Spotify. Try again.");
        }
    }
    memset(verifier, 0, sizeof verifier);
    free(code);
    return ESP_OK;
}

// ------------------------------------------------------------------ lifecycle

static void stop_cb(void *arg) { pairing_stop(); }

esp_err_t pairing_start(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        esp_timer_create_args_t ta = { .callback = stop_cb, .name = "pair_stop" };
        if (!s_lock || esp_timer_create(&ta, &s_stop_timer) != ESP_OK) return ESP_ERR_NO_MEM;
    }
    char ip[16];
    net_ip_str(ip, sizeof ip);
    if (!ip[0]) return ESP_ERR_INVALID_STATE;
    if (s_server) return ESP_OK;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 10240;             // /callback runs the TLS token exchange
    cfg.max_open_sockets = 4;
    cfg.lru_purge_enable = true;        // browsers hold idle connections open
    cfg.max_uri_handlers = 4;
    httpd_handle_t h = NULL;
    esp_err_t err = httpd_start(&h, &cfg);
    if (err != ESP_OK) return err;
    const httpd_uri_t uris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = h_root },
        { .uri = "/login", .method = HTTP_GET, .handler = h_login },
        { .uri = "/callback", .method = HTTP_GET, .handler = h_callback },
    };
    for (size_t i = 0; i < sizeof uris / sizeof uris[0]; i++) httpd_register_uri_handler(h, &uris[i]);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_server = h;
    s_verifier[0] = s_state[0] = 0;
    s_info = (pairing_info_t){ .state = PAIR_WAITING };
    snprintf(s_info.ip, sizeof s_info.ip, "%s", ip);
    bump();
    xSemaphoreGive(s_lock);
    arm_stop((int64_t)PAIRING_TIMEOUT_MIN * 60 * 1000000);
    ESP_LOGI(TAG, "setup page on http://%s/ (redirect URI %s)", ip, pairing_redirect_uri());
    return ESP_OK;
}

void pairing_stop(void)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    httpd_handle_t h = s_server;
    s_server = NULL;
    s_verifier[0] = s_state[0] = 0;
    if (s_info.state != PAIR_OFF) {
        s_info.state = PAIR_OFF;
        s_info.error[0] = 0;
        bump();
    }
    xSemaphoreGive(s_lock);
    esp_timer_stop(s_stop_timer);
    if (h) {
        httpd_stop(h);                  // outside the lock: a running handler may need it
        ESP_LOGI(TAG, "setup page stopped");
    }
}

bool pairing_active(void) { return s_info.state != PAIR_OFF; }

uint32_t pairing_version(void) { return s_version; }

void pairing_get(pairing_info_t *out)
{
    if (!s_lock) { *out = (pairing_info_t){ 0 }; return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_info;
    xSemaphoreGive(s_lock);
}
