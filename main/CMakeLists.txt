idf_component_register(
	SRCS
		"main.c"
		"left_node/left_node.c"
		"right_node/right_node.c"
	INCLUDE_DIRS
		"."
		"left_node"
		"right_node"
	REQUIRES
		esp_driver_gpio
		esp_event
		esp_netif
		esp_timer
		esp_wifi  # esp_now API is provided by esp_wifi in ESP-IDF
		nvs_flash
)

