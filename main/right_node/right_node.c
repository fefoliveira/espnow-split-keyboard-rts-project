#include "right_node.h"

#include <stdbool.h>

#include "esp_err.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "protocol.h"

static const char *TAG = "RIGHT_NODE";

static void wifi_init_sta_for_espnow(void)
{
    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));
}

static const char *key_name(keyboard_key_id_t key_id)
{
    switch (key_id) {
    case KEY_A:
        return "KEY_A";
    case KEY_B:
        return "KEY_B";
    case KEY_ARROW_RIGHT:
        return "KEY_ARROW_RIGHT";
    case KEY_ARROW_LEFT:
        return "KEY_ARROW_LEFT";
    case KEY_ARROW_DOWN:
        return "KEY_ARROW_DOWN";
    case KEY_ARROW_UP:
        return "KEY_ARROW_UP";
    default:
        return "KEY_UNKNOWN";
    }
}

static void handle_received_event(const uint8_t *data, int len)
{
    if (data == NULL || len != (int)sizeof(key_event_t)) {
        ESP_LOGW(TAG, "Invalid ESP-NOW payload size: %d", len);
        return;
    }

    const key_event_t *event = (const key_event_t *)data;

    ESP_LOGI(
        TAG,
        "Key event: id=0x%02X (%s), state=%s",
        (unsigned int)event->key_id,
        key_name(event->key_id),
        event->is_pressed ? "PRESSED" : "RELEASED"
    );
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static void espnow_recv_cb(
    const esp_now_recv_info_t *recv_info,
    const uint8_t *data,
    int len
)
{
    if (recv_info == NULL || recv_info->src_addr == NULL) {
        ESP_LOGW(TAG, "ESP-NOW callback received no source");
        return;
    }

    handle_received_event(data, len);
}
#else
static void espnow_recv_cb(
    const uint8_t *source,
    const uint8_t *data,
    int len
)
{
    if (source == NULL) {
        ESP_LOGW(TAG, "ESP-NOW callback received no source");
        return;
    }

    handle_received_event(data, len);
}
#endif

void right_node_start(void)
{
    ESP_LOGI(TAG, "Starting RIGHT half (ESP-NOW key event receiver)");

    wifi_init_sta_for_espnow();

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

    ESP_LOGI(TAG, "Receiver ready. Waiting for key events...");
}
