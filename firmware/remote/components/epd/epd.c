// SSD1680 / GDEY0213B74 driver. Command sequence per the GDEY0213B74 and SSD1680
// datasheets (review/gdey0213b74.pdf, review/ssd1680.pdf); behavior cross-checked
// against the GxEPD2 driver used for bring-up. Requirements: FIRMWARE-NOTES.md
// "Display sequence and refresh review" - bounded BUSY waits, controlled
// recovery, hardware reset to wake from deep sleep, full refresh after reset.
#include <string.h>
#include "epd.h"
#include "pins.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "epd";

#define EPD_SPI_HOST        SPI2_HOST
#define EPD_SPI_HZ          (4 * 1000 * 1000)   // bench-proven rate; SSD1680 allows 20 MHz write
#define BUSY_TIMEOUT_FULL   6000                // ms; typical 1.7 s on the bench, 3 s datasheet
#define BUSY_TIMEOUT_PART   3000
#define BUSY_TIMEOUT_CMD    500

static spi_device_handle_t s_spi;
static bool s_initialized;      // controller configured since last reset/sleep
static bool s_prev_valid;       // controller's previous-frame RAM matches the panel
static bool s_analog_on;
static epd_stats_t s_stats;
static epd_orient_t s_orient = EPD_ORIENT_NATIVE;
static uint8_t s_tx[EPD_FB_SIZE];   // framebuffer after orientation transform

static inline bool px_get(const uint8_t *fb, int x, int y)
{
    return fb[y * EPD_STRIDE + (x >> 3)] & (0x80 >> (x & 7));
}

// Returns the buffer to send: fb itself when native, else the transformed copy.
static const uint8_t *orient(const uint8_t *fb)
{
    if (s_orient == EPD_ORIENT_NATIVE) return fb;
    memset(s_tx, 0xFF, sizeof s_tx);   // padding bits (x >= 122) stay white
    for (int y = 0; y < EPD_HEIGHT; y++) {
        uint8_t *row = &s_tx[y * EPD_STRIDE];
        for (int x = 0; x < EPD_WIDTH; x++) {
            int sx = x, sy = y;
            if (s_orient == EPD_ORIENT_ROT180 || s_orient == EPD_ORIENT_FLIP_H) sx = EPD_WIDTH - 1 - x;
            if (s_orient == EPD_ORIENT_ROT180 || s_orient == EPD_ORIENT_FLIP_V) sy = EPD_HEIGHT - 1 - y;
            if (!px_get(fb, sx, sy)) row[x >> 3] &= ~(0x80 >> (x & 7));
        }
    }
    return s_tx;
}

void epd_set_orientation(epd_orient_t o)
{
    if (o > EPD_ORIENT_FLIP_H) return;
    if (o != s_orient) s_prev_valid = false;   // controller's previous frame no longer matches
    s_orient = o;
}

epd_orient_t epd_orientation(void) { return s_orient; }

const char *epd_orientation_name(epd_orient_t o)
{
    static const char *n[] = { "native", "rot180", "flipv", "fliph" };
    return o <= EPD_ORIENT_FLIP_H ? n[o] : "?";
}

static void cmd(uint8_t c)
{
    gpio_set_level(PIN_EPD_DC, 0);
    spi_transaction_t t = { .length = 8, .tx_buffer = &c };
    spi_device_polling_transmit(s_spi, &t);
}

static void data(const uint8_t *d, size_t len)
{
    gpio_set_level(PIN_EPD_DC, 1);
    // Chunk to stay within the bus max_transfer_sz.
    while (len) {
        size_t n = len > 4000 ? 4000 : len;
        spi_transaction_t t = { .length = n * 8, .tx_buffer = d };
        spi_device_polling_transmit(s_spi, &t);
        d += n;
        len -= n;
    }
}

static void data1(uint8_t v) { data(&v, 1); }

static esp_err_t wait_busy(uint32_t timeout_ms, uint32_t *elapsed_ms)
{
    int64_t t0 = esp_timer_get_time();
    // BUSY may take a moment to assert after a command.
    while (gpio_get_level(PIN_EPD_BUSY)) {
        int64_t ms = (esp_timer_get_time() - t0) / 1000;
        if (ms > timeout_ms) {
            if (elapsed_ms) *elapsed_ms = ms;
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(ms < 50 ? 1 : 10));
    }
    if (elapsed_ms) *elapsed_ms = (esp_timer_get_time() - t0) / 1000;
    return ESP_OK;
}

// For refreshes: BUSY can lag the 0x20 command slightly, so first wait for it
// to assert. A refresh that never asserts BUSY means the panel did not respond.
static esp_err_t wait_refresh(uint32_t timeout_ms, uint32_t *elapsed_ms)
{
    int64_t t0 = esp_timer_get_time();
    while (!gpio_get_level(PIN_EPD_BUSY)) {
        if (esp_timer_get_time() - t0 > 50 * 1000) {
            ESP_LOGW(TAG, "BUSY never asserted after refresh command");
            return ESP_ERR_INVALID_RESPONSE;
        }
        esp_rom_delay_us(100);
    }
    esp_err_t err = wait_busy(timeout_ms, NULL);
    if (elapsed_ms) *elapsed_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);   // from command
    return err;
}

static void hw_reset(void)
{
    gpio_set_level(PIN_EPD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_EPD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

static void set_window_full(void)
{
    cmd(0x11); data1(0x03);                          // data entry: X+, Y+
    cmd(0x44); data1(0x00); data1(EPD_STRIDE - 1);   // RAM X window, bytes
    cmd(0x45); data1(0x00); data1(0x00);             // RAM Y window 0..249
    data1((EPD_HEIGHT - 1) & 0xFF); data1((EPD_HEIGHT - 1) >> 8);
    cmd(0x4E); data1(0x00);                          // X counter
    cmd(0x4F); data1(0x00); data1(0x00);             // Y counter
}

static esp_err_t controller_init(void)
{
    hw_reset();
    esp_err_t err = wait_busy(BUSY_TIMEOUT_CMD, NULL);
    if (err != ESP_OK) return err;
    cmd(0x12);                                       // software reset
    vTaskDelay(pdMS_TO_TICKS(10));
    if ((err = wait_busy(BUSY_TIMEOUT_CMD, NULL)) != ESP_OK) return err;

    cmd(0x01);                                       // driver output: 250 gates
    data1((EPD_HEIGHT - 1) & 0xFF); data1((EPD_HEIGHT - 1) >> 8); data1(0x00);
    cmd(0x3C); data1(0x05);                          // border waveform
    cmd(0x21); data1(0x00); data1(0x80);             // display update control
    cmd(0x18); data1(0x80);                          // internal temperature sensor
    set_window_full();
    if ((err = wait_busy(BUSY_TIMEOUT_CMD, NULL)) != ESP_OK) return err;

    s_initialized = true;
    s_prev_valid = false;
    s_analog_on = false;
    return ESP_OK;
}

static void write_ram(uint8_t ram_cmd, const uint8_t *fb)
{
    set_window_full();
    cmd(ram_cmd);
    data(fb, EPD_FB_SIZE);
}

static void recover(const char *what)
{
    // Controlled recovery: the controller state is unknown, so force a
    // re-init and a full refresh next time.
    ESP_LOGE(TAG, "BUSY timeout during %s; resetting panel controller", what);
    s_stats.timeouts++;
    s_initialized = false;
    s_prev_valid = false;
    s_analog_on = false;
    hw_reset();
}

esp_err_t epd_update(const uint8_t *fb, epd_refresh_t mode)
{
    esp_err_t err;
    if (!s_initialized && (err = controller_init()) != ESP_OK) {
        recover("init");
        return err;
    }
    if (!s_prev_valid) mode = EPD_REFRESH_FULL;
    fb = orient(fb);

    uint32_t ms = 0;
    if (mode == EPD_REFRESH_FULL) {
        write_ram(0x26, fb);                         // previous = new
        write_ram(0x24, fb);                         // current = new
        cmd(0x22); data1(0xF7);                      // full: clk+analog on, temp, LUT1, display, off
        cmd(0x20);
        err = wait_refresh(BUSY_TIMEOUT_FULL, &ms);
        s_analog_on = false;
    } else {
        write_ram(0x24, fb);                         // current = new; RAM 0x26 holds previous
        cmd(0x22); data1(0xFC);                      // partial: LUT2 (differential), analog stays on
        cmd(0x20);
        err = wait_refresh(BUSY_TIMEOUT_PART, &ms);
        s_analog_on = true;
    }
    if (err != ESP_OK) {
        recover(mode == EPD_REFRESH_FULL ? "full refresh" : "partial refresh");
        return err;
    }
    if (mode == EPD_REFRESH_PARTIAL) write_ram(0x26, fb);   // new becomes previous
    s_prev_valid = true;

    s_stats.last_ms = ms;
    s_stats.last_mode = mode;
    if (mode == EPD_REFRESH_FULL) s_stats.full_count++; else s_stats.partial_count++;
    ESP_LOGI(TAG, "%s refresh %lu ms", mode == EPD_REFRESH_FULL ? "full" : "partial", (unsigned long)ms);
    return ESP_OK;
}

esp_err_t epd_power_off(void)
{
    if (!s_initialized || !s_analog_on) return ESP_OK;
    cmd(0x22); data1(0x83);                          // disable analog + clock
    cmd(0x20);
    esp_err_t err = wait_busy(BUSY_TIMEOUT_CMD, NULL);
    if (err != ESP_OK) { recover("power off"); return err; }
    s_analog_on = false;
    return ESP_OK;
}

esp_err_t epd_sleep(void)
{
    if (!s_initialized) return ESP_OK;
    epd_power_off();
    cmd(0x10); data1(0x01);                          // deep sleep mode 1 (RAM not retained)
    s_initialized = false;
    s_prev_valid = false;
    return ESP_OK;
}

epd_stats_t epd_stats(void) { return s_stats; }

esp_err_t epd_init(void)
{
    gpio_set_level(PIN_EPD_RST, 1);
    gpio_set_level(PIN_EPD_DC, 1);
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_EPD_RST) | (1ULL << PIN_EPD_DC),
        .mode = GPIO_MODE_OUTPUT,
    };
    esp_err_t err = gpio_config(&out);
    if (err != ESP_OK) return err;
    gpio_config_t in = {
        .pin_bit_mask = 1ULL << PIN_EPD_BUSY,
        .mode = GPIO_MODE_INPUT,
    };
    if ((err = gpio_config(&in)) != ESP_OK) return err;

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_EPD_MOSI, .miso_io_num = -1, .sclk_io_num = PIN_EPD_SCK,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 4096,
    };
    if ((err = spi_bus_initialize(EPD_SPI_HOST, &bus, SPI_DMA_CH_AUTO)) != ESP_OK) return err;
    spi_device_interface_config_t dev = {
        .clock_speed_hz = EPD_SPI_HZ, .mode = 0, .spics_io_num = PIN_EPD_CS, .queue_size = 1,
    };
    return spi_bus_add_device(EPD_SPI_HOST, &dev, &s_spi);
}
