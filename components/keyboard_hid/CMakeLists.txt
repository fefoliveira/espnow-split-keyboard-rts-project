set(srcs "keyboard_hid.c")
set(include_dirs "include")

if(CONFIG_KEYBOARD_OUTPUT_BLE_HID)
    list(APPEND srcs "esp_hid_gap/esp_hid_gap.c")
    list(APPEND include_dirs "esp_hid_gap")
endif()

idf_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${include_dirs}"
    REQUIRES
        bt
        esp_hid
        shared_protocol
)
