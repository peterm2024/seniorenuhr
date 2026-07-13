idf_component_register(
    SRCS "app_main.c" "anzeige.c" "netz.c" "zeit.c"
    INCLUDE_DIRS "."
    REQUIRES fonts kalender
    PRIV_REQUIRES esp_lcd esp_driver_i2c esp_driver_gpio esp_wifi esp_netif esp_event nvs_flash
)
