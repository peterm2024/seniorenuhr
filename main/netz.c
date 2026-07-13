#include "netz.h"
#include "secrets.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

static const char *TAG = "netz";

#define BIT_HAT_IP BIT0

static EventGroupHandle_t s_events;
static volatile bool s_verbunden = false;

static void ereignis_handler(void *arg, esp_event_base_t basis, int32_t id, void *daten)
{
    (void)arg; (void)daten;

    if (basis == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (basis == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_verbunden = false;
        ESP_LOGW(TAG, "WLAN-Verbindung verloren, versuche erneut...");
        esp_wifi_connect();
    } else if (basis == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_verbunden = true;
        ESP_LOGI(TAG, "WLAN verbunden, IP-Adresse erhalten");
        xEventGroupSetBits(s_events, BIT_HAT_IP);
    }
}

esp_err_t netz_start(uint32_t timeout_ms)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &ereignis_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ereignis_handler, NULL));

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, WLAN_SSID, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, WLAN_PASSWORT, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.sta.pmf_cfg.capable = true;
    wifi_cfg.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Verbinde mit WLAN '%s'...", WLAN_SSID);
    EventBits_t bits = xEventGroupWaitBits(s_events, BIT_HAT_IP, pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    return (bits & BIT_HAT_IP) ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool netz_ist_verbunden(void)
{
    return s_verbunden;
}
