idf_component_register(
    SRCS "button_debounce.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_driver_gpio
        shared_protocol
)
