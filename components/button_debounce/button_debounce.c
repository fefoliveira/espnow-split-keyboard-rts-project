#include "button_debounce.h"

#include <stdint.h>

esp_err_t button_debounce_init(
    debounced_button_t *buttons,
    size_t button_count
)
{
    if (buttons == NULL || button_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint64_t pin_bit_mask = 0;

    for (size_t i = 0; i < button_count; i++) {
        if (!GPIO_IS_VALID_GPIO(buttons[i].gpio)) {
            return ESP_ERR_INVALID_ARG;
        }

        pin_bit_mask |= 1ULL << buttons[i].gpio;
    }

    gpio_config_t io_config = {
        .pin_bit_mask = pin_bit_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_config);

    if (err != ESP_OK) {
        return err;
    }

    for (size_t i = 0; i < button_count; i++) {
        bool is_pressed = gpio_get_level(buttons[i].gpio) == 0;

        buttons[i].stable_pressed = is_pressed;
        buttons[i].candidate_pressed = is_pressed;
        buttons[i].candidate_samples = 0;
    }

    return ESP_OK;
}

bool button_debounce_sample(
    debounced_button_t *button,
    uint16_t required_samples,
    key_event_t *event
)
{
    if (button == NULL || event == NULL || required_samples == 0) {
        return false;
    }

    bool sampled_pressed = gpio_get_level(button->gpio) == 0;

    if (sampled_pressed != button->candidate_pressed) {
        button->candidate_pressed = sampled_pressed;
        button->candidate_samples = 1;
        return false;
    }

    if (button->candidate_samples < required_samples) {
        button->candidate_samples++;
    }

    if (button->candidate_samples < required_samples ||
        button->stable_pressed == button->candidate_pressed) {
        return false;
    }

    button->stable_pressed = button->candidate_pressed;
    event->key_id = button->key_id;
    event->is_pressed = button->stable_pressed;

    return true;
}
