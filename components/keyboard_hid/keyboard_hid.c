#include "keyboard_hid.h"

#include <string.h>

#include "esp_log.h"

#if CONFIG_KEYBOARD_OUTPUT_BLE_HID
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_hid_gap.h"
#include "esp_hid_common.h"
#include "esp_hidd.h"
#include "esp_hidd_gatts.h"
#endif

static const char *TAG = "KEYBOARD_HID";

#if CONFIG_KEYBOARD_OUTPUT_BLE_HID
#define HID_REPORT_ID_KEYBOARD 1
#define HID_KEYBOARD_REPORT_LEN 8

static esp_hidd_dev_t *s_hid_dev;
static bool s_hid_connected;

static const unsigned char s_keyboard_report_map[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0xE0,        //   Usage Minimum (0xE0)
    0x29, 0xE7,        //   Usage Maximum (0xE7)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x03,        //   Input (Const,Var,Abs)
    0x95, 0x05,        //   Report Count (5)
    0x75, 0x01,        //   Report Size (1)
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (Num Lock)
    0x29, 0x05,        //   Usage Maximum (Kana)
    0x91, 0x02,        //   Output (Data,Var,Abs)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x03,        //   Report Size (3)
    0x91, 0x03,        //   Output (Const,Var,Abs)
    0x95, 0x05,        //   Report Count (5)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0x00,        //   Usage Minimum (0x00)
    0x29, 0x65,        //   Usage Maximum (0x65)
    0x81, 0x00,        //   Input (Data,Array,Abs)
    0xC0,              // End Collection
};

static esp_hid_raw_report_map_t s_report_maps[] = {
    {
        .data = s_keyboard_report_map,
        .len = sizeof(s_keyboard_report_map),
    },
};

static esp_hid_device_config_t s_hid_config = {
    .vendor_id = 0x16C0,
    .product_id = 0x05DF,
    .version = 0x0100,
    .device_name = "ESP Split Keyboard",
    .manufacturer_name = "Uergs",
    .serial_number = "espnow-split-keyboard",
    .report_maps = s_report_maps,
    .report_maps_len = 1,
};

void ble_hid_task_start_up(void)
{
    /*
     * The ESP-IDF GAP helper is shared with HID examples and calls this hook
     * after BLE security completes. This project sends reports from the right
     * node consolidation task, so there is no example task to start here.
     */
}

static void keyboard_hid_event_callback(
    void *handler_args,
    esp_event_base_t base,
    int32_t id,
    void *event_data
)
{
    esp_hidd_event_t event = (esp_hidd_event_t)id;
    esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;

    (void)handler_args;
    (void)base;

    switch (event) {
    case ESP_HIDD_START_EVENT:
        ESP_LOGI(TAG, "BLE HID started; advertising as \"%s\"", s_hid_config.device_name);
        ESP_ERROR_CHECK(esp_hid_ble_gap_adv_start());
        break;
    case ESP_HIDD_CONNECT_EVENT:
        s_hid_connected = true;
        ESP_LOGI(TAG, "BLE HID connected");
        break;
    case ESP_HIDD_DISCONNECT_EVENT:
        s_hid_connected = false;
        ESP_LOGI(
            TAG,
            "BLE HID disconnected: %s",
            esp_hid_disconnect_reason_str(
                esp_hidd_dev_transport_get(param->disconnect.dev),
                param->disconnect.reason
            )
        );
        ESP_ERROR_CHECK(esp_hid_ble_gap_adv_start());
        break;
    case ESP_HIDD_OUTPUT_EVENT:
        ESP_LOGI(TAG, "BLE HID output report received, len=%d", param->output.length);
        break;
    default:
        break;
    }
}

static void add_pressed_key(uint8_t *report, size_t *slot, bool is_pressed, keyboard_key_id_t key_id)
{
    if (!is_pressed || *slot >= HID_KEYBOARD_REPORT_LEN) {
        return;
    }

    report[*slot] = (uint8_t)key_id;
    (*slot)++;
}

esp_err_t keyboard_hid_init(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing BLE HID keyboard output");

    ret = esp_hid_gap_init(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_KEYBOARD, s_hid_config.device_name);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_ble_gatts_register_callback(esp_hidd_gatts_event_handler);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_hidd_dev_init(
        &s_hid_config,
        ESP_HID_TRANSPORT_BLE,
        keyboard_hid_event_callback,
        &s_hid_dev
    );
    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

esp_err_t keyboard_hid_send_state(const keyboard_hid_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_hid_dev == NULL || !s_hid_connected) {
        return ESP_OK;
    }

    uint8_t report[HID_KEYBOARD_REPORT_LEN] = {0};
    size_t slot = 2;

    add_pressed_key(report, &slot, state->key_a, KEY_A);
    add_pressed_key(report, &slot, state->key_b, KEY_B);
    add_pressed_key(report, &slot, state->arrow_up, KEY_ARROW_UP);
    add_pressed_key(report, &slot, state->arrow_down, KEY_ARROW_DOWN);
    add_pressed_key(report, &slot, state->arrow_left, KEY_ARROW_LEFT);
    add_pressed_key(report, &slot, state->arrow_right, KEY_ARROW_RIGHT);

    return esp_hidd_dev_input_set(
        s_hid_dev,
        0,
        HID_REPORT_ID_KEYBOARD,
        report,
        sizeof(report)
    );
}

#else

esp_err_t keyboard_hid_init(void)
{
    ESP_LOGI(TAG, "BLE HID output disabled; using serial/log output mode");
    return ESP_OK;
}

esp_err_t keyboard_hid_send_state(const keyboard_hid_state_t *state)
{
    (void)state;
    return ESP_OK;
}

#endif
