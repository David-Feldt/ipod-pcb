// LVGL <-> SSD1680 glue and the refresh scheduler.
// LVGL renders the whole 1-bit frame (FULL render mode, ~4 KB) and the flush
// callback copies it into the framebuffer; the UI task then decides when and
// how (partial/full) to push it to the panel.
#include <string.h>
#include "ui.h"
#include "epd.h"
#include "lvgl.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "ui";

#define COALESCE_MS       80      // gather near-simultaneous changes into one refresh
#define POWER_OFF_IDLE_MS 1500    // turn off panel analog supply after partials go idle
#define PALETTE_BYTES     8       // LVGL I1 buffers start with a 2-entry palette

static SemaphoreHandle_t s_mutex;
static uint8_t s_draw_buf[EPD_FB_SIZE + PALETTE_BYTES] __attribute__((aligned(4)));
static uint8_t s_fb[EPD_FB_SIZE];           // latest frame from LVGL (guarded by s_mutex)
static uint8_t s_out[EPD_FB_SIZE];          // snapshot being sent to the panel
static volatile bool s_dirty;
static volatile bool s_full_req = true;     // first frame is always full
static int64_t s_dirty_since_ms;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }
static uint32_t tick_cb(void) { return (uint32_t)now_ms(); }

void ui_lock(void) { xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY); }
void ui_unlock(void) { xSemaphoreGiveRecursive(s_mutex); }

void ui_request_full_refresh(void)
{
    ui_lock();
    s_full_req = true;
    if (!s_dirty) { s_dirty = true; s_dirty_since_ms = now_ms(); }
    ui_unlock();
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    // FULL render mode: area is always the whole screen.
    const uint8_t *src = px_map + PALETTE_BYTES;
    // LVGL I1: bit 1 = light pixel; SSD1680: bit 1 = white. Same polarity.
    if (memcmp(s_fb, src, EPD_FB_SIZE) != 0) {
        memcpy(s_fb, src, EPD_FB_SIZE);
        if (!s_dirty) { s_dirty = true; s_dirty_since_ms = now_ms(); }
    }
    lv_display_flush_ready(disp);
}

static void ui_task(void *arg)
{
    int partials = 0;
    int64_t last_partial_ms = 0;
    bool analog_on = false;

    for (;;) {
        ui_lock();
        uint32_t wait = lv_timer_handler();
        bool go = s_dirty && now_ms() - s_dirty_since_ms >= COALESCE_MS;
        bool full = false;
        if (go) {
            memcpy(s_out, s_fb, EPD_FB_SIZE);       // latest state wins
            full = s_full_req || partials >= CONFIG_REMOTE_FULL_REFRESH_EVERY;
            s_dirty = false;
            s_full_req = false;
        }
        ui_unlock();

        if (go) {
            // Refresh outside the lock so other tasks can keep updating state;
            // whatever changes meanwhile is picked up on the next pass.
            esp_err_t err = epd_update(s_out, full ? EPD_REFRESH_FULL : EPD_REFRESH_PARTIAL);
            if (err != ESP_OK) {
                ui_request_full_refresh();          // driver reset itself; redo as full
                vTaskDelay(pdMS_TO_TICKS(1000));
            } else if (epd_stats().last_mode == EPD_REFRESH_FULL) {
                partials = 0;
                analog_on = false;
            } else {
                partials++;
                analog_on = true;
                last_partial_ms = now_ms();
            }
        } else if (analog_on && now_ms() - last_partial_ms >= POWER_OFF_IDLE_MS) {
            epd_power_off();
            analog_on = false;
        }

        if (wait > 50) wait = 50;
        if (wait < 5) wait = 5;
        vTaskDelay(pdMS_TO_TICKS(wait));
    }
}

esp_err_t ui_init(void)
{
    s_mutex = xSemaphoreCreateRecursiveMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;
    esp_err_t err = epd_init();
    if (err != ESP_OK) return err;

    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *disp = lv_display_create(EPD_WIDTH, EPD_HEIGHT);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_I1);
    lv_display_set_buffers(disp, s_draw_buf, NULL, sizeof s_draw_buf, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(disp, flush_cb);
    memset(s_fb, 0xFF, sizeof s_fb);

    ESP_LOGI(TAG, "LVGL %d.%d.%d on %dx%d e-paper, full refresh every %d partials",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH, EPD_WIDTH, EPD_HEIGHT,
             CONFIG_REMOTE_FULL_REFRESH_EVERY);
    return xTaskCreate(ui_task, "ui", 6144, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
