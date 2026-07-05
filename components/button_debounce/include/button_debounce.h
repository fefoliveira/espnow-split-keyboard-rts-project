#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "protocol.h"

typedef struct {
    gpio_num_t gpio;
    keyboard_key_id_t key_id;
    bool stable_pressed;
    bool candidate_pressed;
    uint16_t candidate_samples;
} debounced_button_t;

esp_err_t button_debounce_init(
    debounced_button_t *buttons,
    size_t button_count
);

bool button_debounce_sample(
    debounced_button_t *button,
    uint16_t required_samples,
    key_event_t *event
);
