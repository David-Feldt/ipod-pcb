// Wi-Fi and Bluetooth are exclusive modes (no PSRAM). The mode is chosen at
// boot: a restart request survives in RTC memory once, so any other reset
// (power-on, crash, deep-sleep wake) starts in Wi-Fi mode again.
#include "board.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_log.h"

#define MAGIC_BT        0xB1E7004Du    // automatic fallback
#define MAGIC_BT_MANUAL 0xB1E7004Eu    // chosen from the menu/console

static RTC_NOINIT_ATTR uint32_t s_request;
static board_mode_t s_mode = MODE_WIFI;
static bool s_manual;

board_mode_t board_boot_mode(void)
{
    static bool taken;
    if (!taken) {
        taken = true;
        bool sw = esp_reset_reason() == ESP_RST_SW;
        s_mode = sw && (s_request == MAGIC_BT || s_request == MAGIC_BT_MANUAL) ? MODE_BLUETOOTH : MODE_WIFI;
        s_manual = sw && s_request == MAGIC_BT_MANUAL;
        s_request = 0;
    }
    return s_mode;
}

bool board_mode_manual(void) { board_boot_mode(); return s_manual; }

void board_restart_into(board_mode_t m, bool manual)
{
    ESP_LOGW("mode", "restarting into %s mode%s", m == MODE_BLUETOOTH ? "Bluetooth" : "Wi-Fi", manual ? " (chosen)" : "");
    s_request = m == MODE_BLUETOOTH ? (manual ? MAGIC_BT_MANUAL : MAGIC_BT) : 0;
    esp_restart();
}
