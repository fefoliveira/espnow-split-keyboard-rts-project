#include "right_node.h"

#include <inttypes.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "RIGHT_NODE";

/* Common onboard LED pin for ESP32 DevKit boards. Adjust if needed. */
#define ONBOARD_LED_GPIO GPIO_NUM_2

static QueueHandle_t s_led_event_queue;

typedef struct {
    uint32_t counter;
    uint64_t uptime_ms;
    char text[64];
} espnow_message_t;

static void onboard_led_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << ONBOARD_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&io_conf));
    ESP_ERROR_CHECK(gpio_set_level(ONBOARD_LED_GPIO, 0));
}

static void led_blink_task(void *arg)
{
    uint8_t event;

    (void)arg;

    while (1) {
        if (xQueueReceive(s_led_event_queue, &event, portMAX_DELAY) == pdTRUE) {
            ESP_ERROR_CHECK(gpio_set_level(ONBOARD_LED_GPIO, 1));
            vTaskDelay(pdMS_TO_TICKS(100));
            ESP_ERROR_CHECK(gpio_set_level(ONBOARD_LED_GPIO, 0));
        }
    }
}

static void notify_led_blink(void)
{
    uint8_t event = 1;

    if (s_led_event_queue == NULL) {
        return;
    }

    if (xQueueSend(s_led_event_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "LED event queue full, dropping blink event");
    }
}

static void wifi_init_sta_for_espnow(void)
{
    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));
}

static void log_received_payload(const uint8_t *data, int len)
{
    if (len == (int)sizeof(espnow_message_t)) {
        espnow_message_t message;

        memcpy(&message, data, sizeof(message));
        message.text[sizeof(message.text) - 1] = '\0';

        ESP_LOGI(
            TAG,
            "Message payload: counter=%" PRIu32 ", uptime_ms=%" PRIu64 ", text=\"%s\"",
            message.counter,
            message.uptime_ms,
            message.text
        );
        return;
    }

    ESP_LOGI(TAG, "Raw payload received (%d bytes)", len);
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    const uint8_t *src_mac = recv_info != NULL ? recv_info->src_addr : NULL;

    if (src_mac == NULL || data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Invalid data in ESP-NOW recv callback");
        return;
    }

    ESP_LOGI(
        TAG,
        "Received %d bytes from %02X:%02X:%02X:%02X:%02X:%02X",
        len,
        src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5]
    );

    notify_led_blink();
    log_received_payload(data, len);
}
#else
static void espnow_recv_cb(const uint8_t *src_mac, const uint8_t *data, int len)
{
    if (src_mac == NULL || data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Invalid data in ESP-NOW recv callback");
        return;
    }

    ESP_LOGI(
        TAG,
        "Received %d bytes from %02X:%02X:%02X:%02X:%02X:%02X",
        len,
        src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5]
    );

    notify_led_blink();
    log_received_payload(data, len);
}
#endif

void right_node_start(void)
{
    ESP_LOGI(TAG, "Starting RIGHT half (ESP-NOW receiver)");

    wifi_init_sta_for_espnow();

    onboard_led_init();
    s_led_event_queue = xQueueCreate(10, sizeof(uint8_t));
    ESP_ERROR_CHECK(s_led_event_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(
        xTaskCreate(led_blink_task, "led_blink_task", 2048, NULL, 5, NULL) == pdPASS
            ? ESP_OK
            : ESP_FAIL
    );

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

    ESP_LOGI(TAG, "Receiver ready. Waiting for ESP-NOW packets...");
}
