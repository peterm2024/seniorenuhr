idf_component_register(
    SRCS "app_main.c" "anzeige.c"
    INCLUDE_DIRS "."
    REQUIRES fonts kalender
    PRIV_REQUIRES esp_lcd esp_driver_i2c esp_driver_gpio
)
