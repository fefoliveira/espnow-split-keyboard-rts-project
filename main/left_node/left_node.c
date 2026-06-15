#include "left_node.h"

#include <stdbool.h>
#include <string.h>

#include "button_debounce.h"
#include "esp_err.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "protocol.h"

static const char *TAG = "LEFT_NODE";

#define LEFT_KEY_A_GPIO GPIO_NUM_25
#define LEFT_KEY_B_GPIO GPIO_NUM_26
#define BUTTON_SCAN_PERIOD_MS 1
#define BUTTON_DEBOUNCE_MS 5

static const uint8_t BROADCAST_MAC[ESP_NOW_ETH_ALEN] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

static debounced_button_t s_buttons[] = {
    {.gpio = LEFT_KEY_A_GPIO, .key_id = KEY_A},
    {.gpio = LEFT_KEY_B_GPIO, .key_id = KEY_B},
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
    const uint8_t *destination = tx_info != NULL ? tx_info->des_addr : NULL;

    if (destination == NULL) {
        ESP_LOGE(TAG, "Send callback received no destination");
        return;
    }

    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGW(TAG, "ESP-NOW delivery failed");
    }
}
#else
static void espnow_send_cb(const uint8_t *destination, esp_now_send_status_t status)
{
    if (destination == NULL) {
        ESP_LOGE(TAG, "Send callback received no destination");
        return;
    }

    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGW(TAG, "ESP-NOW delivery failed");
    }
}
#endif

static void send_key_event(const key_event_t *event)
{
    ESP_ERROR_CHECK(
        esp_now_send(
            BROADCAST_MAC,
            (const uint8_t *)event,
            sizeof(*event)
        )
    );
}

static void left_button_scan_task(void *arg)
{
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t scan_period = pdMS_TO_TICKS(BUTTON_SCAN_PERIOD_MS);

    (void)arg;

    while (true) {
        for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); i++) {
            key_event_t event;

            if (button_debounce_sample(
                    &s_buttons[i],
                    BUTTON_DEBOUNCE_MS,
                    &event
                )) {
                send_key_event(&event);
            }
        }

        xTaskDelayUntil(&last_wake_time, scan_period);
    }
}

void left_node_start(void)
{
    esp_now_peer_info_t peer = {0};
    esp_err_t err;

    ESP_LOGI(
        TAG,
        "Starting LEFT scanner: KEY_A=GPIO%d, KEY_B=GPIO%d, polling=1000 Hz",
        LEFT_KEY_A_GPIO,
        LEFT_KEY_B_GPIO
    );

    wifi_init_sta_for_espnow();
    ESP_ERROR_CHECK(
        button_debounce_init(
            s_buttons,
            sizeof(s_buttons) / sizeof(s_buttons[0])
        )
    );

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));

    memcpy(peer.peer_addr, BROADCAST_MAC, ESP_NOW_ETH_ALEN);
    peer.channel = 1;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;

    err = esp_now_add_peer(&peer);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        ESP_ERROR_CHECK(err);
    }

    ESP_ERROR_CHECK(
        xTaskCreate(
            left_button_scan_task,
            "left_button_scan_task",
            4096,
            NULL,
            configMAX_PRIORITIES - 1,
            NULL
        ) == pdPASS
            ? ESP_OK
            : ESP_ERR_NO_MEM
    );
}
