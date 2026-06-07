#include "left_node.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "LEFT_NODE";

typedef struct {
    uint32_t counter;
    uint64_t uptime_ms;
    char text[64];
} espnow_message_t;

static const uint8_t BROADCAST_MAC[ESP_NOW_ETH_ALEN] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

static void wifi_init_sta_for_espnow(void)
{
    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
static void espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    const uint8_t *mac_addr = tx_info != NULL ? tx_info->des_addr : NULL;

    if (mac_addr == NULL) {
        ESP_LOGE(TAG, "Send callback with NULL destination MAC");
        return;
    }

    ESP_LOGI(
        TAG,
        "Send status to %02X:%02X:%02X:%02X:%02X:%02X = %s",
        mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5],
        status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAIL"
    );
}
#else
static void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    if (mac_addr == NULL) {
        ESP_LOGE(TAG, "Send callback with NULL MAC");
        return;
    }

    ESP_LOGI(
        TAG,
        "Send status to %02X:%02X:%02X:%02X:%02X:%02X = %s",
        mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5],
        status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAIL"
    );
}
#endif

static void left_ping_task(void *arg)
{
    uint32_t counter = 0;

    (void)arg;

    while (1) {
        espnow_message_t message = {
            .counter = counter,
            .uptime_ms = (uint64_t)(esp_timer_get_time() / 1000)
        };

        snprintf(message.text, sizeof(message.text), "Mensagem ESP-NOW #%" PRIu32, counter);
        counter++;

        ESP_LOGI(TAG, "Sending message: \"%s\"", message.text);

        esp_err_t err = esp_now_send(
            BROADCAST_MAC,
            (const uint8_t *)&message,
            sizeof(message)
        );

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_now_send failed: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(
                TAG,
                "Message sent: counter=%" PRIu32 ", uptime_ms=%" PRIu64 ", text=\"%s\"",
                message.counter,
                message.uptime_ms,
                message.text
            );
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void left_node_start(void)
{
    esp_now_peer_info_t peer = {0};
    esp_err_t err;

    ESP_LOGI(TAG, "Starting LEFT half (ESP-NOW transmitter)");

    wifi_init_sta_for_espnow();

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));

    memcpy(peer.peer_addr, BROADCAST_MAC, ESP_NOW_ETH_ALEN);
    peer.channel = 1;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;

    err = esp_now_add_peer(&peer);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGE(TAG, "esp_now_add_peer failed: %s", esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    }

    ESP_ERROR_CHECK(
        xTaskCreate(left_ping_task, "left_ping_task", 4096, NULL, 5, NULL) == pdPASS
            ? ESP_OK
            : ESP_FAIL
    );
}
