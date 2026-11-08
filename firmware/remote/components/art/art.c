#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "art.h"
#include "esp32s3/rom/tjpgd.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "art";

#define URL_MAX      160
#define POOL_SIZE    4096          // TJpgDec work area (needs ~3.1 KB)

static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static int s_size;
static char s_req_url[URL_MAX];           // newest request (guarded by s_lock)
static char s_res_url[URL_MAX];           // URL of the last finished request
static bool s_res_ok;
static uint8_t *s_res;                    // packed 1-bit result
static volatile uint32_t s_version;

// ---- streaming fetch + decode ---------------------------------------------
// The JPEG is decoded straight from the HTTP stream, so no buffer for the
// whole file is needed (only TLS, the 4 KB decoder pool and the scaler).

typedef struct {
    esp_http_client_handle_t http;
    bool eof;
    int out_w, out_h;               // decoded (scaled) image size
    int size;                       // target edge
    uint16_t *sum;                  // per target pixel: sum of luma
    uint8_t *cnt;                   // and number of source pixels
    size_t bytes;
} dec_t;

static UINT jpeg_in(JDEC *jd, BYTE *buf, UINT n)
{
    dec_t *d = jd->device;
    uint8_t skip[64];
    UINT done = 0;
    while (done < n && !d->eof) {
        char *dst = buf ? (char *)buf + done : (char *)skip;
        int want = buf ? (int)(n - done) : (int)(n - done < sizeof skip ? n - done : sizeof skip);
        int got = esp_http_client_read(d->http, dst, want);
        if (got <= 0) { d->eof = true; break; }
        done += got;
    }
    d->bytes += done;
    return done;
}

// Box-filter each decoded block into the target grid.
static UINT jpeg_out(JDEC *jd, void *bitmap, JRECT *r)
{
    dec_t *d = jd->device;
    const uint8_t *px = bitmap;
    for (int y = r->top; y <= r->bottom; y++) {
        int ty = y * d->size / d->out_h;
        for (int x = r->left; x <= r->right; x++, px += 3) {
            if (ty >= d->size) continue;
            int tx = x * d->size / d->out_w;
            if (tx >= d->size) continue;
            int luma = (px[0] * 77 + px[1] * 150 + px[2] * 29) >> 8;
            int i = ty * d->size + tx;
            if (d->cnt[i] < 255 && d->sum[i] <= 65535 - 255) { d->sum[i] += luma; d->cnt[i]++; }
        }
    }
    return 1;
}

// Stretch contrast (1st..99th percentile), then Atkinson dither to packed 1-bit.
static void dither(int16_t *g, int n, uint8_t *out)
{
    int hist[256] = { 0 };
    for (int i = 0; i < n * n; i++) hist[g[i]]++;
    int lo = 0, hi = 255, acc = 0, cut = n * n / 100;
    for (lo = 0; lo < 255 && (acc += hist[lo]) <= cut; lo++) {}
    acc = 0;
    for (hi = 255; hi > 0 && (acc += hist[hi]) <= cut; hi--) {}
    if (hi - lo < 32) { lo = 0; hi = 255; }        // flat image: leave it alone
    for (int i = 0; i < n * n; i++) {
        int v = (g[i] - lo) * 255 / (hi - lo);
        g[i] = v < 0 ? 0 : v > 255 ? 255 : v;
    }
    memset(out, 0, ART_BYTES(n));
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            int i = y * n + x;
            int v = g[i];
            int o = v < 128 ? 0 : 255;
            if (o) out[y * ART_STRIDE(n) + (x >> 3)] |= 0x80 >> (x & 7);
            int e = (v - o) / 8;                      // Atkinson: 6/8 of the error, 1/8 each
            if (x + 1 < n) g[i + 1] += e;
            if (x + 2 < n) g[i + 2] += e;
            if (y + 1 < n) {
                if (x > 0) g[i + n - 1] += e;
                g[i + n] += e;
                if (x + 1 < n) g[i + n + 1] += e;
            }
            if (y + 2 < n) g[i + 2 * n] += e;
        }
    }
}

static esp_err_t fetch_convert(const char *url, int size, uint8_t *out)
{
    static uint8_t pool[POOL_SIZE];
    // One client kept for the life of the task: it holds the TLS session
    // ticket, so after the first cover the handshake skips the slow
    // public-key work (~2 s at 240 MHz without it). The connection itself
    // is closed between covers.
    static esp_http_client_handle_t http;
    int64_t t0 = esp_timer_get_time();
    if (!http) {
        esp_http_client_config_t cfg = {
            .url = url, .timeout_ms = 10000, .crt_bundle_attach = esp_crt_bundle_attach, .buffer_size = 2048,
            .save_client_session = true,
        };
        if (!(http = esp_http_client_init(&cfg))) return ESP_ERR_NO_MEM;
    } else {
        esp_http_client_set_url(http, url);
    }
    dec_t d = { .size = size, .http = http };
    esp_err_t err = esp_http_client_open(d.http, 0);
    int64_t t_open = esp_timer_get_time();
    int64_t clen = err == ESP_OK ? esp_http_client_fetch_headers(d.http) : -1;
    int status = esp_http_client_get_status_code(d.http);
    int64_t t_hdr = esp_timer_get_time();
    if (err == ESP_OK && status != 200) { ESP_LOGW(TAG, "HTTP %d", status); err = ESP_FAIL; }

    JDEC jd;
    if (err == ESP_OK) {
        JRESULT r = jd_prepare(&jd, jpeg_in, pool, sizeof pool, &d);
        if (r != JDR_OK) { ESP_LOGW(TAG, "jd_prepare: %d (progressive JPEG?)", r); err = ESP_FAIL; }
    }
    if (err == ESP_OK) {
        int scale = 0;                               // largest 1/2^s that still covers the target
        while (scale < 3 && (int)(jd.width >> (scale + 1)) >= size && (int)(jd.height >> (scale + 1)) >= size) scale++;
        d.out_w = jd.width >> scale;
        d.out_h = jd.height >> scale;
        d.sum = calloc(size * size, sizeof *d.sum);
        d.cnt = calloc(size * size, 1);
        if (!d.sum || !d.cnt) err = ESP_ERR_NO_MEM;
        else {
            JRESULT r = jd_decomp(&jd, jpeg_out, scale);
            if (r != JDR_OK) { ESP_LOGW(TAG, "jd_decomp: %d", r); err = ESP_FAIL; }
        }
    }
    esp_http_client_close(d.http);
    int64_t t_dec = esp_timer_get_time();

    if (err == ESP_OK) {
        int16_t *g = (int16_t *)d.sum;               // reuse: averages fit in int16
        for (int i = 0; i < size * size; i++) g[i] = d.cnt[i] ? d.sum[i] / d.cnt[i] : 255;
        dither(g, size, out);
    }
    free(d.sum);
    free(d.cnt);
    ESP_LOGI(TAG, "%s: %u/%lld bytes; connect %lld ms, headers %lld ms, body+decode %lld ms, dither %lld ms; "
             "min free heap %u", err == ESP_OK ? "ok" : esp_err_to_name(err), (unsigned)d.bytes, clen,
             (t_open - t0) / 1000, (t_hdr - t_open) / 1000, (t_dec - t_hdr) / 1000,
             (esp_timer_get_time() - t_dec) / 1000, (unsigned)esp_get_minimum_free_heap_size());
    return err;
}

// ---- task -------------------------------------------------------------------

static void task(void *arg)
{
    uint8_t *work = malloc(ART_BYTES(s_size));
    char url[URL_MAX];
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        snprintf(url, sizeof url, "%s", s_req_url);
        bool same = s_res_ok && !strcmp(url, s_res_url);   // failed ones may be retried
        xSemaphoreGive(s_lock);
        if (same) continue;

        bool ok = url[0] && work && fetch_convert(url, s_size, work) == ESP_OK;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        snprintf(s_res_url, sizeof s_res_url, "%s", url);
        s_res_ok = ok;
        if (ok) memcpy(s_res, work, ART_BYTES(s_size));
        s_version++;
        xSemaphoreGive(s_lock);
    }
}

void art_request(const char *url)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(s_req_url, sizeof s_req_url, "%s", url ? url : "");
    xSemaphoreGive(s_lock);
    xTaskNotifyGive(s_task);
}

uint32_t art_version(void) { return s_version; }

bool art_get(char *url_out, size_t n, uint8_t *pixels)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(url_out, n, "%s", s_res_url);
    bool ok = s_res_ok;
    if (ok) memcpy(pixels, s_res, ART_BYTES(s_size));
    xSemaphoreGive(s_lock);
    return ok;
}

esp_err_t art_start(int size)
{
    if (size <= 0 || size > ART_MAX_SIZE) return ESP_ERR_INVALID_ARG;
    s_size = size;
    s_lock = xSemaphoreCreateMutex();
    s_res = malloc(ART_BYTES(size));
    if (!s_lock || !s_res) return ESP_ERR_NO_MEM;
    return xTaskCreate(task, "art", 8192, NULL, 3, &s_task) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
