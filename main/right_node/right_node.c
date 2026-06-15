#include "right_node.h"

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
#include "freertos/queue.h"
#include "freertos/task.h"
#include "protocol.h"

static const char *TAG = "RIGHT_NODE";

#define RIGHT_ARROW_UP_GPIO GPIO_NUM_27
#define RIGHT_ARROW_DOWN_GPIO GPIO_NUM_33
#define RIGHT_ARROW_LEFT_GPIO GPIO_NUM_25
#define RIGHT_ARROW_RIGHT_GPIO GPIO_NUM_26

#define BUTTON_SCAN_PERIOD_MS 1
#define BUTTON_DEBOUNCE_MS 5
#define KEY_EVENT_QUEUE_LENGTH 32
#define CONSOLIDATION_TASK_PRIORITY 3

typedef struct {
    bool key_a;
    bool key_b;
    bool arrow_up;
    bool arrow_down;
    bool arrow_left;
    bool arrow_right;
} keyboard_state_t;

typedef struct {
    gpio_num_t gpio;
    keyboard_key_id_t key_id;
    bool stable_pressed;
    bool candidate_pressed;
    uint8_t candidate_samples;
} button_state_t;

static keyboard_state_t s_keyboard_state = {0};
static QueueHandle_t s_key_event_queue;

static button_state_t s_local_buttons[] = {
    {.gpio = RIGHT_ARROW_UP_GPIO, .key_id = KEY_ARROW_UP},
    {.gpio = RIGHT_ARROW_DOWN_GPIO, .key_id = KEY_ARROW_DOWN},
    {.gpio = RIGHT_ARROW_LEFT_GPIO, .key_id = KEY_ARROW_LEFT},
    {.gpio = RIGHT_ARROW_RIGHT_GPIO, .key_id = KEY_ARROW_RIGHT},
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

static void local_buttons_init(void)
{
    gpio_config_t io_config = {
        .pin_bit_mask =
            (1ULL << RIGHT_ARROW_UP_GPIO) |
            (1ULL << RIGHT_ARROW_DOWN_GPIO) |
            (1ULL << RIGHT_ARROW_LEFT_GPIO) |
            (1ULL << RIGHT_ARROW_RIGHT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&io_config));

    for (size_t i = 0; i < sizeof(s_local_buttons) / sizeof(s_local_buttons[0]); i++) {
        bool is_pressed = gpio_get_level(s_local_buttons[i].gpio) == 0;

        s_local_buttons[i].stable_pressed = is_pressed;
        s_local_buttons[i].candidate_pressed = is_pressed;
        s_local_buttons[i].candidate_samples = 0;
    }
}

static bool is_valid_key_id(keyboard_key_id_t key_id)
{
    switch (key_id) {
    case KEY_A:
    case KEY_B:
    case KEY_ARROW_UP:
    case KEY_ARROW_DOWN:
    case KEY_ARROW_LEFT:
    case KEY_ARROW_RIGHT:
        return true;
    default:
        return false;
    }
}

static void enqueue_remote_event(const uint8_t *data, int len)
{
    if (data == NULL || len != (int)sizeof(key_event_t) || s_key_event_queue == NULL) {
        return;
    }

    key_event_t event;
    memcpy(&event, data, sizeof(event));

    if (!is_valid_key_id(event.key_id)) {
        return;
    }

    (void)xQueueSend(s_key_event_queue, &event, 0);
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static void espnow_recv_cb(
    const esp_now_recv_info_t *recv_info,
    const uint8_t *data,
    int len
)
{
    if (recv_info == NULL || recv_info->src_addr == NULL) {
        return;
    }

    enqueue_remote_event(data, len);
}
#else
static void espnow_recv_cb(
    const uint8_t *source,
    const uint8_t *data,
    int len
)
{
    if (source == NULL) {
        return;
    }

    enqueue_remote_event(data, len);
}
#endif

static void enqueue_local_event(const button_state_t *button)
{
    key_event_t event = {
        .key_id = button->key_id,
        .is_pressed = button->stable_pressed,
    };

    if (xQueueSend(s_key_event_queue, &event, 0) != pdPASS) {
        ESP_LOGW(TAG, "Key event queue full; local event dropped");
    }
}

static void scan_local_button(button_state_t *button)
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
        enqueue_local_event(button);
    }
}

static void right_local_scan_task(void *arg)
{
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t scan_period = pdMS_TO_TICKS(BUTTON_SCAN_PERIOD_MS);

    (void)arg;

    while (true) {
        for (size_t i = 0; i < sizeof(s_local_buttons) / sizeof(s_local_buttons[0]); i++) {
            scan_local_button(&s_local_buttons[i]);
        }

        xTaskDelayUntil(&last_wake_time, scan_period);
    }
}

static void update_keyboard_state(const key_event_t *event)
{
    switch (event->key_id) {
    case KEY_A:
        s_keyboard_state.key_a = event->is_pressed;
        break;
    case KEY_B:
        s_keyboard_state.key_b = event->is_pressed;
        break;
    case KEY_ARROW_UP:
        s_keyboard_state.arrow_up = event->is_pressed;
        break;
    case KEY_ARROW_DOWN:
        s_keyboard_state.arrow_down = event->is_pressed;
        break;
    case KEY_ARROW_LEFT:
        s_keyboard_state.arrow_left = event->is_pressed;
        break;
    case KEY_ARROW_RIGHT:
        s_keyboard_state.arrow_right = event->is_pressed;
        break;
    default:
        break;
    }
}

static void log_keyboard_state(void)
{
    ESP_LOGI(
        TAG,
        "Keyboard state: A=%d B=%d UP=%d DOWN=%d LEFT=%d RIGHT=%d",
        s_keyboard_state.key_a,
        s_keyboard_state.key_b,
        s_keyboard_state.arrow_up,
        s_keyboard_state.arrow_down,
        s_keyboard_state.arrow_left,
        s_keyboard_state.arrow_right
    );
}

static void keyboard_consolidation_task(void *arg)
{
    key_event_t event;

    (void)arg;

    while (true) {
        if (xQueueReceive(s_key_event_queue, &event, portMAX_DELAY) == pdPASS) {
            update_keyboard_state(&event);
            log_keyboard_state();
        }
    }
}

void right_node_start(void)
{
    ESP_LOGI(
        TAG,
        "Starting RIGHT half: UP=GPIO%d DOWN=GPIO%d LEFT=GPIO%d RIGHT=GPIO%d",
        RIGHT_ARROW_UP_GPIO,
        RIGHT_ARROW_DOWN_GPIO,
        RIGHT_ARROW_LEFT_GPIO,
        RIGHT_ARROW_RIGHT_GPIO
    );

    s_key_event_queue = xQueueCreate(KEY_EVENT_QUEUE_LENGTH, sizeof(key_event_t));
    ESP_ERROR_CHECK(s_key_event_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);

    local_buttons_init();
    wifi_init_sta_for_espnow();

    ESP_ERROR_CHECK(
        xTaskCreate(
            keyboard_consolidation_task,
            "keyboard_consolidation_task",
            4096,
            NULL,
            CONSOLIDATION_TASK_PRIORITY,
            NULL
        ) == pdPASS
            ? ESP_OK
            : ESP_ERR_NO_MEM
    );

    ESP_ERROR_CHECK(
        xTaskCreate(
            right_local_scan_task,
            "right_local_scan_task",
            4096,
            NULL,
            configMAX_PRIORITIES - 2,
            NULL
        ) == pdPASS
            ? ESP_OK
            : ESP_ERR_NO_MEM
    );

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

    ESP_LOGI(TAG, "Central key-event queue ready");
}
