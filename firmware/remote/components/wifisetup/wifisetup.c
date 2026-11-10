#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "wifisetup.h"
#include "dns.h"
#include "net.h"
#include "settings.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "wifisetup";

#define AP_URL          "http://192.168.4.1/"
#define TRY_TIMEOUT_MS  25000
#define DONE_LINGER_US  (12 * 1000000LL)    // let the phone see "Connected", then stop
#define MAX_NETS        16

static SemaphoreHandle_t s_lock;
static httpd_handle_t s_server;
static esp_timer_handle_t s_stop_timer;
static volatile uint32_t s_version;
static wifisetup_info_t s_info;
static net_scan_entry_t s_nets[MAX_NETS];
static int s_nnets;
static char s_try_pass[65];

static void bump(void) { s_version++; }

static void arm_stop(int64_t us)
{
    esp_timer_stop(s_stop_timer);
    esp_timer_start_once(s_stop_timer, us);
}

static void arm_idle(void) { arm_stop((int64_t)WIFISETUP_TIMEOUT_MIN * 60 * 1000000); }

// ------------------------------------------------------------------ text helpers

static void html_escape(char *out, size_t n, const char *in)
{
    size_t o = 0;
    for (; *in && o + 7 < n; in++) {
        const char *rep = NULL;
        switch (*in) {
        case '&': rep = "&amp;"; break;
        case '<': rep = "&lt;"; break;
        case '>': rep = "&gt;"; break;
        case '"': rep = "&quot;"; break;
        case '\'': rep = "&#39;"; break;
        }
        if (rep) { size_t l = strlen(rep); memcpy(out + o, rep, l); o += l; }
        else out[o++] = *in;
    }
    out[o] = 0;
}

// In place: '+' -> space, %XX -> byte.
static void url_decode(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (*s == '+') *o++ = ' ';
        else if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char h[3] = { s[1], s[2], 0 };
            *o++ = (char)strtol(h, NULL, 16);
            s += 2;
        } else *o++ = *s;
    }
    *o = 0;
}

void wifisetup_join_qr(const wifisetup_info_t *i, char *out, size_t n)
{
    // Wi-Fi QR format; \ ; , : " must be escaped (ours never contain them,
    // but keep it correct).
    char ssid[48], pass[32];
    const char *src[2] = { i->ap_ssid, i->ap_pass };
    char *dst[2] = { ssid, pass };
    size_t cap[2] = { sizeof ssid, sizeof pass };
    for (int k = 0; k < 2; k++) {
        size_t o = 0;
        for (const char *c = src[k]; *c && o + 2 < cap[k]; c++) {
            if (strchr("\\;,:\"", *c)) dst[k][o++] = '\\';
            dst[k][o++] = *c;
        }
        dst[k][o] = 0;
    }
    snprintf(out, n, "WIFI:T:WPA;S:%s;P:%s;;", ssid, pass);
}

// ------------------------------------------------------------------ pages

static void page_start(httpd_req_t *req, int refresh_s)
{
    char head[1024];
    snprintf(head, sizeof head,
             "<!doctype html><html><head><meta charset=utf-8>"
             "<meta name=viewport content=\"width=device-width,initial-scale=1\">%s"
             "<title>E-ink Remote Wi-Fi</title><style>"
             "body{font:17px/1.45 system-ui,sans-serif;max-width:26em;margin:1.5em auto;padding:0 1em;color:#111}"
             "h1{font-size:1.4em}label.net{display:block;padding:.55em .3em;border-bottom:1px solid #ddd}"
             "input[type=text],input[type=password]{width:100%%;box-sizing:border-box;font-size:17px;padding:.5em;margin:.3em 0}"
             "button{background:#111;color:#fff;font-size:17px;font-weight:600;border:0;border-radius:2em;padding:.8em 1.6em;margin-top:.6em}"
             ".err{background:#fde8e8;border-left:4px solid #c00;padding:.6em .8em}"
             ".ok{background:#e6f6ea;border-left:4px solid #1db954;padding:.6em .8em}"
             "small{color:#666}</style></head><body><h1>E-ink Remote Wi-Fi</h1>",
             refresh_s > 0 ? "<meta http-equiv=refresh content=2>" : "");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, head);
}

static void page_end(httpd_req_t *req)
{
    httpd_resp_sendstr_chunk(req, "</body></html>");
    httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t h_root(httpd_req_t *req)
{
    wifisetup_info_t p;
    wifisetup_get(&p);
    char line[640], esc[200];
    if (p.state == WS_CONNECTING) {
        page_start(req, 2);
        html_escape(esc, sizeof esc, p.target);
        snprintf(line, sizeof line, "<p>Connecting to <b>%s</b>&hellip;</p>"
                 "<p><small>If this page stops updating, check the remote's screen.</small></p>", esc);
        httpd_resp_sendstr_chunk(req, line);
        page_end(req);
        return ESP_OK;
    }
    if (p.state == WS_DONE) {
        page_start(req, 0);
        html_escape(esc, sizeof esc, p.target);
        snprintf(line, sizeof line, "<p class=ok>Connected to <b>%s</b>.</p>", esc);
        httpd_resp_sendstr_chunk(req, line);
        httpd_resp_sendstr_chunk(req, "<p>The remote turns this hotspot off in a few seconds and your phone "
                                      "goes back to its usual Wi-Fi. If the remote isn't signed in to Spotify "
                                      "yet, it then shows a code to scan for that.</p>");
        page_end(req);
        return ESP_OK;
    }

    arm_idle();                         // someone is using the page
    page_start(req, 0);
    if (p.error[0]) {
        html_escape(esc, sizeof esc, p.error);
        snprintf(line, sizeof line, "<p class=err>%s</p>", esc);
        httpd_resp_sendstr_chunk(req, line);
    }
    httpd_resp_sendstr_chunk(req, "<form method=post action=/connect><p>Choose the Wi-Fi network for the remote "
                                  "<small>(2.4 GHz only)</small>:</p>");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = s_nnets;
    net_scan_entry_t nets[MAX_NETS];
    memcpy(nets, s_nets, sizeof nets);
    xSemaphoreGive(s_lock);
    for (int i = 0; i < n; i++) {
        const char *bars = nets[i].rssi > -60 ? "&#9679;&#9679;&#9679;" : nets[i].rssi > -72 ? "&#9679;&#9679;&#9675;"
                                                                                           : "&#9679;&#9675;&#9675;";
        html_escape(esc, sizeof esc, nets[i].ssid);
        snprintf(line, sizeof line, "<label class=net><input type=radio name=ssid value=\"%s\"%s> %s "
                 "<small>%s%s</small></label>", esc, i == 0 ? " checked" : "", esc, bars,
                 nets[i].open ? " open" : "");
        httpd_resp_sendstr_chunk(req, line);
    }
    snprintf(line, sizeof line, "<label class=net><input type=radio name=ssid value=\"\"%s> Other (hidden) network:"
             "<input type=text name=other maxlength=32 autocapitalize=off autocorrect=off></label>",
             n == 0 ? " checked" : "");
    httpd_resp_sendstr_chunk(req, line);
    httpd_resp_sendstr_chunk(req,
        "<p>Password<input type=password id=pw name=pass maxlength=63 autocapitalize=off autocorrect=off>"
        "<label><input type=checkbox onclick=\"pw.type=this.checked?'text':'password'\"> show</label></p>"
        "<button type=submit>Connect</button></form>"
        "<p><small><a href=/scan>Scan again</a> &middot; The password is only used to join your network "
        "and is stored on the remote.</small></p>");
    page_end(req);
    return ESP_OK;
}

static void redirect(httpd_req_t *req, const char *status, const char *to)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_hdr(req, "Location", to);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, NULL, 0);
}

static void set_waiting_error(const char *msg)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_info.state != WS_OFF) {
        s_info.state = WS_WAITING;
        snprintf(s_info.error, sizeof s_info.error, "%s", msg);
        bump();
    }
    xSemaphoreGive(s_lock);
}

static void try_task(void *arg)
{
    char ssid[33];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(ssid, sizeof ssid, "%s", s_info.target);
    xSemaphoreGive(s_lock);

    net_try_t r = net_try(ssid, s_try_pass, TRY_TIMEOUT_MS);
    if (r == NET_TRY_OK) {
        esp_err_t e1 = settings_set_str(SETTING_WIFI_SSID, ssid);
        esp_err_t e2 = settings_set_str(SETTING_WIFI_PASS, s_try_pass);
        if (e1 != ESP_OK || e2 != ESP_OK) ESP_LOGE(TAG, "could not save Wi-Fi settings");
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_info.state == WS_CONNECTING) {
            s_info.state = WS_DONE;
            s_info.error[0] = 0;
            net_ip_str(s_info.ip, sizeof s_info.ip);
            bump();
        }
        xSemaphoreGive(s_lock);
        ESP_LOGI(TAG, "joined \"%s\"; saved", ssid);
        arm_stop(DONE_LINGER_US);
    } else {
        char msg[96];
        snprintf(msg, sizeof msg, "%s",
                 r == NET_TRY_WRONG_PASSWORD ? "Wrong password. Try again." :
                 r == NET_TRY_NOT_FOUND ? "Couldn't find that network. Is it 2.4 GHz and in range?" :
                 "Couldn't connect to that network. Try again.");
        ESP_LOGW(TAG, "\"%s\": %s", ssid, msg);
        set_waiting_error(msg);
    }
    memset(s_try_pass, 0, sizeof s_try_pass);
    vTaskDelete(NULL);
}

static esp_err_t h_connect(httpd_req_t *req)
{
    char body[400];
    int len = req->content_len < sizeof body - 1 ? req->content_len : sizeof body - 1;
    int got = 0;
    while (got < len) {
        int r = httpd_req_recv(req, body + got, len - got);
        if (r <= 0) return ESP_FAIL;
        got += r;
    }
    body[got] = 0;
    char ssid[100] = "", other[100] = "", pass[200] = "";
    httpd_query_key_value(body, "ssid", ssid, sizeof ssid);
    httpd_query_key_value(body, "other", other, sizeof other);
    httpd_query_key_value(body, "pass", pass, sizeof pass);
    url_decode(ssid);
    url_decode(other);
    url_decode(pass);
    const char *target = ssid[0] ? ssid : other;
    size_t pl = strlen(pass);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool busy = s_info.state != WS_WAITING;
    xSemaphoreGive(s_lock);
    if (busy) { redirect(req, "303 See Other", "/"); return ESP_OK; }

    if (!target[0] || strlen(target) > 32) {
        set_waiting_error("Choose a network (names are at most 32 characters).");
    } else if (pl && (pl < 8 || pl > 63)) {
        set_waiting_error("Wi-Fi passwords are 8 to 63 characters.");
    } else {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        snprintf(s_info.target, sizeof s_info.target, "%.32s", target);   // length checked above
        snprintf(s_try_pass, sizeof s_try_pass, "%.63s", pass);
        s_info.state = WS_CONNECTING;
        s_info.error[0] = 0;
        bump();
        xSemaphoreGive(s_lock);
        arm_idle();
        if (xTaskCreate(try_task, "wifi_try", 4096, NULL, 5, NULL) != pdPASS)
            set_waiting_error("The remote is out of memory. Try again.");
    }
    memset(pass, 0, sizeof pass);
    memset(body, 0, sizeof body);
    redirect(req, "303 See Other", "/");
    return ESP_OK;
}

static void rescan(void)
{
    net_scan_entry_t nets[MAX_NETS];
    int n = net_scan(nets, MAX_NETS);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(s_nets, nets, sizeof nets);
    s_nnets = n;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "%d networks found", n);
}

static esp_err_t h_scan(httpd_req_t *req)
{
    if (s_info.state == WS_WAITING) rescan();
    redirect(req, "303 See Other", "/");
    return ESP_OK;
}

// Everything else (phones' connectivity checks, any other site): send to the
// setup page. A non-"success" answer is what makes the phone open it.
static esp_err_t h_any(httpd_req_t *req)
{
    redirect(req, "302 Found", AP_URL);
    return ESP_OK;
}

// ------------------------------------------------------------------ lifecycle

static void stop_cb(void *arg) { wifisetup_stop(); }

esp_err_t wifisetup_start(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        esp_timer_create_args_t ta = { .callback = stop_cb, .name = "wifisetup_stop" };
        if (!s_lock || esp_timer_create(&ta, &s_stop_timer) != ESP_OK) return ESP_ERR_NO_MEM;
    }
    if (s_server) return ESP_OK;

    // Hotspot name from the MAC (stable per board), password fresh each time.
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    static const char alphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";    // no 0/o, 1/l/i
    char pass[11];
    for (int i = 0; i < 10; i++) pass[i] = alphabet[esp_random() % (sizeof alphabet - 1)];
    pass[10] = 0;
    char ssid[24];
    snprintf(ssid, sizeof ssid, "E-ink Remote %02X%02X", mac[4], mac[5]);

    esp_err_t err = net_ap_start(ssid, pass);
    if (err != ESP_OK) return err;
    rescan();                           // before anyone joins: scanning hops channels
    dns_start();

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 6144;
    cfg.max_open_sockets = 5;
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 5;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    httpd_handle_t h = NULL;
    if ((err = httpd_start(&h, &cfg)) != ESP_OK) {
        dns_stop();
        net_ap_stop();
        return err;
    }
    const httpd_uri_t uris[] = {    // matched in this order; "/*" catches the rest
        { .uri = "/", .method = HTTP_GET, .handler = h_root },
        { .uri = "/connect", .method = HTTP_POST, .handler = h_connect },
        { .uri = "/scan", .method = HTTP_GET, .handler = h_scan },
        { .uri = "/*", .method = HTTP_GET, .handler = h_any },
    };
    for (size_t i = 0; i < sizeof uris / sizeof uris[0]; i++) httpd_register_uri_handler(h, &uris[i]);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_server = h;
    s_info = (wifisetup_info_t){ .state = WS_WAITING };
    snprintf(s_info.ap_ssid, sizeof s_info.ap_ssid, "%s", ssid);
    snprintf(s_info.ap_pass, sizeof s_info.ap_pass, "%s", pass);
    bump();
    xSemaphoreGive(s_lock);
    arm_idle();
    ESP_LOGI(TAG, "hotspot \"%s\" up; setup page " AP_URL, ssid);
    return ESP_OK;
}

void wifisetup_stop(void)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    httpd_handle_t h = s_server;
    s_server = NULL;
    if (s_info.state != WS_OFF) {
        s_info.state = WS_OFF;
        bump();
    }
    xSemaphoreGive(s_lock);
    esp_timer_stop(s_stop_timer);
    if (h) {
        httpd_stop(h);
        dns_stop();
        net_ap_stop();
        ESP_LOGI(TAG, "hotspot stopped");
    }
}

bool wifisetup_active(void) { return s_info.state != WS_OFF; }

uint32_t wifisetup_version(void) { return s_version; }

void wifisetup_get(wifisetup_info_t *out)
{
    if (!s_lock) { *out = (wifisetup_info_t){ 0 }; return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_info;
    xSemaphoreGive(s_lock);
}
