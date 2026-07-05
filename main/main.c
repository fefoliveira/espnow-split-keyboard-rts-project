#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "left_node/left_node.h"
#include "right_node/right_node.h"

static const char *TAG = "APP_MAIN";

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

#ifdef CONFIG_KEYBOARD_HALF_LEFT
    ESP_LOGI(TAG, "Configured as LEFT half");
    left_node_start();
#elif defined(CONFIG_KEYBOARD_HALF_RIGHT)
    ESP_LOGI(TAG, "Configured as RIGHT half");
    right_node_start();
#else
#error "No keyboard half selected. Use menuconfig > Keyboard Configuration > Target Device"
#endif
}
