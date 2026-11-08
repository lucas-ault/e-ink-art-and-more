idf_component_register(
    SRCS "main.c" "comm.c" "display.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_http_server nvs_flash esp_netif esp_event
             esp_driver_gpio esp_driver_spi esp_timer
)
