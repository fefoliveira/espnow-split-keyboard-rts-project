#ifndef SHARED_PROTOCOL_PROTOCOL_H
#define SHARED_PROTOCOL_PROTOCOL_H

#include <stdbool.h>

typedef enum {
    KEY_A = 0x04,
    KEY_B = 0x05,
    KEY_ARROW_RIGHT = 0x4F,
    KEY_ARROW_LEFT = 0x50,
    KEY_ARROW_DOWN = 0x51,
    KEY_ARROW_UP = 0x52,
} keyboard_key_id_t;

typedef struct {
    keyboard_key_id_t key_id;
    bool is_pressed;
} key_event_t;

#endif
