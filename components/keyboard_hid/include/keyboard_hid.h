#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "protocol.h"

typedef struct {
    bool key_a;
    bool key_b;
    bool arrow_up;
    bool arrow_down;
    bool arrow_left;
    bool arrow_right;
} keyboard_hid_state_t;

esp_err_t keyboard_hid_init(void);
esp_err_t keyboard_hid_send_state(const keyboard_hid_state_t *state);
