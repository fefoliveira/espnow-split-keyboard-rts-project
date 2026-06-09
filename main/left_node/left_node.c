#include "left_node.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
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

typedef struct {
    gpio_num_t gpio;
    keyboard_key_id_t key_id;
    bool stable_pressed;
    bool candidate_pressed;
    uint8_t candidate_samples;
} button_state_t;

static const uint8_t BROADCAST_MAC[ESP_NOW_ETH_ALEN] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

static button_state_t s_buttons[] = {
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

static void buttons_init(void)
{
    gpio_config_t io_config = {
        .pin_bit_mask = (1ULL << LEFT_KEY_A_GPIO) | (1ULL << LEFT_KEY_B_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&io_config));

    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); i++) {
        bool is_pressed = gpio_get_level(s_buttons[i].gpio) == 0;

        s_buttons[i].stable_pressed = is_pressed;
        s_buttons[i].candidate_pressed = is_pressed;
        s_buttons[i].candidate_samples = 0;
    }
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

static void send_key_event(const button_state_t *button)
{
    key_event_t event = {
        .key_id = button->key_id,
        .is_pressed = button->stable_pressed,
    };

    ESP_ERROR_CHECK(
        esp_now_send(
            BROADCAST_MAC,
            (const uint8_t *)&event,
            sizeof(event)
        )
    );
}

static void scan_button(button_state_t *button)
{
    bool sampled_pressed = gpio_get_level(button->gpio) == 0;

    if (sampled_pressed != button->candidate_pressed) {
        button->candidate_pressed = sampled_pressed;
        button->candidate_samples = 1;
        return;
    }

    if (button->candidate_samples < BUTTON_DEBOUNCE_MS) {
        button->candidate_samples++;
    }

    if (button->candidate_samples >= BUTTON_DEBOUNCE_MS &&
        button->stable_pressed != button->candidate_pressed) {
        button->stable_pressed = button->candidate_pressed;
        send_key_event(button);
    }
}

static void left_button_scan_task(void *arg)
{
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t scan_period = pdMS_TO_TICKS(BUTTON_SCAN_PERIOD_MS);

    (void)arg;

    while (true) {
        for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); i++) {
            scan_button(&s_buttons[i]);
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
    buttons_init();

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
