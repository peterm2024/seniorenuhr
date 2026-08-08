idf_component_register(
    SRCS "ota.c" "screenshot_debug.c" "einstellungen.c" "tagesansicht.c" "app_main.c" "anzeige.c" "netz.c" "zeit.c" "startbildschirm.c" "einrichtung.c"
         "kalender_holen.c" "kalender_speicher.c" "kalender_anzeige.c" "webkonfig.c"
    INCLUDE_DIRS "."
    REQUIRES fonts kalender
    PRIV_REQUIRES esp_lcd esp_driver_i2c esp_driver_gpio esp_wifi esp_netif esp_event nvs_flash
                  esp_http_client esp-tls mbedtls fatfs wear_levelling esp_timer esp_http_server mdns
                  esp_https_ota app_update json
)

# Produktions-Build (Release-Workflow): blendet die Entwicklungswerkzeuge
# aus, ohne app_main.c anzufassen. Ein Datei-Eingriff wuerde den
# Arbeitsbaum "dirty" machen und damit die per "git describe" ermittelte
# Firmware-Version verfaelschen (v0.9.0-dirty statt v0.9.0), was die
# Zuordnung zu den Release-Tags kaputtmacht.
if(DEFINED ENV{PRODUKTIONS_BUILD})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ENTWICKLUNGSWERKZEUGE=0)
    message(STATUS "Produktions-Build: Entwicklungswerkzeuge deaktiviert")
endif()
