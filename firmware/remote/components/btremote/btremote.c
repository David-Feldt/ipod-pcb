#include <string.h>
#include "btremote.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_nimble_hci.h"
#include "esp_hidd.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "btremote";

// Consumer control: report ID 1, one 16-bit usage (0 = released).
static const uint8_t s_report_map[] = {
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x03,  //   Logical Maximum (1023)
    0x19, 0x00,        //   Usage Minimum (0)
    0x2A, 0xFF, 0x03,  //   Usage Maximum (1023)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x00,        //   Input (Data, Array, Absolute)
    0xC0,              // End Collection
};
#define REPORT_ID 1

static esp_hidd_dev_t *s_hid;
static volatile btr_state_t s_state = BTR_OFF;
static volatile uint32_t s_version;
static char s_name[32];
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;

static void set_state(btr_state_t st)
{
    if (st != s_state) ESP_LOGI(TAG, "%s -> %s", btremote_state_name(s_state), btremote_state_name(st));
    s_state = st;
    s_version++;
}

btr_state_t btremote_state(void) { return s_state; }
uint32_t btremote_version(void) { return s_version; }

const char *btremote_state_name(btr_state_t s)
{
    static const char *n[] = { "off", "advertising", "connected", "ready" };
    return s <= BTR_READY ? n[s] : "?";
}

int btremote_bond_count(void)
{
    int n = 0;
    if (s_state == BTR_OFF || ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &n) != 0) return 0;
    return n;
}

static int gap_event(struct ble_gap_event *ev, void *arg);

static void advertise(void)
{
    struct ble_hs_adv_fields f = { 0 };
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.appearance = 0x0180;                      // Generic Remote Control
    f.appearance_is_present = 1;
    static const ble_uuid16_t hid = BLE_UUID16_INIT(0x1812);
    f.uuids16 = &hid;
    f.num_uuids16 = 1;
    f.uuids16_is_complete = 1;
    f.name = (const uint8_t *)s_name;
    f.name_len = strlen(s_name);
    f.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&f);
    if (rc) { ESP_LOGE(TAG, "adv fields: %d", rc); return; }
    struct ble_gap_adv_params p = { 0 };
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    p.itvl_min = BLE_GAP_ADV_ITVL_MS(100);      // phones find it within a second or two
    p.itvl_max = BLE_GAP_ADV_ITVL_MS(150);
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc && rc != BLE_HS_EALREADY) { ESP_LOGE(TAG, "adv start: %d", rc); return; }
    set_state(BTR_ADVERTISING);
}

// Our own GAP handler (security and advertising); esp_hid hooks its own
// listener for the HID connection bookkeeping.
static int gap_event(struct ble_gap_event *ev, void *arg)
{
    struct ble_gap_conn_desc d;
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status != 0) { advertise(); return 0; }
        s_conn = ev->connect.conn_handle;
        set_state(BTR_CONNECTED);
        ble_gap_security_initiate(s_conn);      // pair now (or re-encrypt with the bond)
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected (reason 0x%x)", ev->disconnect.reason);
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        advertise();
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (ev->enc_change.status == 0) set_state(BTR_READY);
        else ESP_LOGW(TAG, "encryption failed: %d", ev->enc_change.status);
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (s_conn == BLE_HS_CONN_HANDLE_NONE) advertise();
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        // The phone forgot us and pairs again: drop the stale bond and accept.
        if (ble_gap_conn_find(ev->repeat_pairing.conn_handle, &d) == 0) ble_store_util_delete_peer(&d.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        // No display/keyboard pairing: Just Works. Accept a numeric comparison.
        struct ble_sm_io io = { .action = ev->passkey.params.action };
        if (io.action == BLE_SM_IOACT_NUMCMP) { io.numcmp_accept = 1; ble_sm_inject_io(ev->passkey.conn_handle, &io); }
        return 0;
    }
    default:
        return 0;
    }
}

static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    advertise();
}

static void on_reset(int reason) { ESP_LOGW(TAG, "host reset: %d", reason); }

static void host_task(void *arg)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void hidd_cb(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == ESP_HIDD_START_EVENT) ESP_LOGI(TAG, "HID service started");
}

void ble_store_config_init(void);

esp_err_t btremote_start(const char *name)
{
    snprintf(s_name, sizeof s_name, "%s", name);
    esp_err_t err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    if ((err = nimble_port_init()) != ESP_OK) { ESP_LOGE(TAG, "nimble init: %s", esp_err_to_name(err)); return err; }

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    static esp_hid_raw_report_map_t maps[] = { { .data = s_report_map, .len = sizeof s_report_map } };
    esp_hid_device_config_t cfg = {
        .vendor_id = 0x303A,                    // Espressif
        .product_id = 0x0E1C,
        .version = 0x0100,
        .device_name = s_name,
        .manufacturer_name = "ipod-pcb",
        .serial_number = "0001",
        .report_maps = maps,
        .report_maps_len = 1,
    };
    if ((err = esp_hidd_dev_init(&cfg, ESP_HID_TRANSPORT_BLE, hidd_cb, &s_hid)) != ESP_OK) {
        ESP_LOGE(TAG, "HID init: %s", esp_err_to_name(err));
        return err;
    }
    ble_svc_gap_device_name_set(s_name);
    ble_svc_gap_device_appearance_set(0x0180);
    ble_store_config_init();
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

esp_err_t btremote_key(uint16_t usage)
{
    if (s_state != BTR_READY || !s_hid) return ESP_ERR_INVALID_STATE;
    uint8_t down[2] = { usage & 0xFF, usage >> 8 }, up[2] = { 0, 0 };
    esp_err_t err = esp_hidd_dev_input_set(s_hid, 0, REPORT_ID, down, sizeof down);
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(30));
        err = esp_hidd_dev_input_set(s_hid, 0, REPORT_ID, up, sizeof up);
    }
    if (err != ESP_OK) ESP_LOGW(TAG, "key 0x%04x: %s", usage, esp_err_to_name(err));
    return err;
}

void btremote_set_battery(int pct)
{
    if (!s_hid || pct < 0) return;
    esp_hidd_dev_battery_set(s_hid, pct > 100 ? 100 : pct);
}

void btremote_forget(void)
{
    if (s_state == BTR_OFF) return;
    if (s_conn != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
    ble_store_clear();
    ESP_LOGI(TAG, "bonds cleared");
    s_version++;
}

void btremote_release_memory(void)
{
    esp_err_t err = esp_bt_mem_release(ESP_BT_MODE_BTDM);
    if (err != ESP_OK) ESP_LOGW(TAG, "bt mem release: %s", esp_err_to_name(err));
}
