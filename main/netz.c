#include "netz.h"
#include "secrets.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NVS_NAMENSRAUM "wifi_cfg"

static const char *TAG = "netz";

/* Reisst die WLAN-Verbindung im Laufbetrieb ab und kommt laenger als diese
 * Zeit nicht wieder, hilft kein Reconnect-Versuch mehr weiter - dann lieber
 * neu starten, statt dauerhaft (z. B. mit haengenden Verbindungen)
 * steckenzubleiben. Der Watchdog ist erst NACH der ersten erfolgreichen
 * Verbindung scharf - waehrend des Bootens ueberwacht stattdessen der
 * 60s-Countdown des Startbildschirms die WLAN-Phase (app_main). */
#define WATCHDOG_GRENZE_US (30LL * 1000000)
#define WATCHDOG_PRUEF_INTERVALL_US (5LL * 1000000)

static volatile bool s_verbunden = false;
static volatile bool s_war_verbunden = false; /* schon je eine IP bekommen? */

/* 0 = aktuell verbunden (oder noch nie verbunden gewesen); sonst Zeitpunkt
 * (esp_timer_get_time), seit dem ununterbrochen keine Verbindung besteht. */
static volatile int64_t s_getrennt_seit_us = 0;

static void wifi_watchdog_callback(void *arg)
{
    (void)arg;
    int64_t getrennt_seit = s_getrennt_seit_us;
    if (getrennt_seit == 0)
        return;
    if (esp_timer_get_time() - getrennt_seit > WATCHDOG_GRENZE_US) {
        ESP_LOGE(TAG, "Seit ueber 30s ohne WLAN-Verbindung - Neustart");
        esp_restart();
    }
}

static void ereignis_handler(void *arg, esp_event_base_t basis, int32_t id, void *daten)
{
    (void)arg; (void)daten;

    if (basis == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (basis == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_verbunden = false;
        if (s_war_verbunden && s_getrennt_seit_us == 0)
            s_getrennt_seit_us = esp_timer_get_time();
        ESP_LOGW(TAG, "WLAN-Verbindung verloren, versuche erneut...");
        esp_wifi_connect();
    } else if (basis == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_verbunden = true;
        s_war_verbunden = true;
        s_getrennt_seit_us = 0;
        ESP_LOGI(TAG, "WLAN verbunden, IP-Adresse erhalten");
    }
}

/* Liest SSID/Passwort aus dem NVS, falls dort per
 * netz_zugangsdaten_speichern() welche hinterlegt wurden. */
static bool zugangsdaten_aus_nvs_lesen(char *ssid, size_t ssid_groesse,
                                        char *passwort, size_t passwort_groesse)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMENSRAUM, NVS_READONLY, &h) != ESP_OK)
        return false;

    size_t ssid_laenge = ssid_groesse;
    size_t passwort_laenge = passwort_groesse;
    esp_err_t err_ssid = nvs_get_str(h, "ssid", ssid, &ssid_laenge);
    esp_err_t err_passwort = nvs_get_str(h, "pass", passwort, &passwort_laenge);
    nvs_close(h);

    return err_ssid == ESP_OK && err_passwort == ESP_OK && ssid_laenge > 1;
}

esp_err_t netz_zugangsdaten_speichern(const char *ssid, const char *passwort)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMENSRAUM, NVS_READWRITE, &h);
    if (err != ESP_OK)
        return err;

    err = nvs_set_str(h, "ssid", ssid);
    if (err == ESP_OK)
        err = nvs_set_str(h, "pass", passwort);
    if (err == ESP_OK)
        err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK)
        return err;

    ESP_LOGI(TAG, "Neue WLAN-Zugangsdaten gespeichert - starte neu");
    esp_restart();
    return ESP_OK; /* unerreichbar */
}

void netz_start(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    const esp_timer_create_args_t watchdog_cfg = {
        .callback = wifi_watchdog_callback,
        .name = "wifi_watchdog",
    };
    esp_timer_handle_t watchdog;
    ESP_ERROR_CHECK(esp_timer_create(&watchdog_cfg, &watchdog));
    ESP_ERROR_CHECK(esp_timer_start_periodic(watchdog, WATCHDOG_PRUEF_INTERVALL_US));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &ereignis_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ereignis_handler, NULL));

    wifi_config_t wifi_cfg = {0};
    const char *quelle;
    if (zugangsdaten_aus_nvs_lesen((char *)wifi_cfg.sta.ssid, sizeof wifi_cfg.sta.ssid,
                                    (char *)wifi_cfg.sta.password, sizeof wifi_cfg.sta.password)) {
        quelle = "NVS (per Einrichtungsbildschirm gespeichert)";
    } else {
        snprintf((char *)wifi_cfg.sta.ssid, sizeof wifi_cfg.sta.ssid, "%s", WLAN_SSID);
        snprintf((char *)wifi_cfg.sta.password, sizeof wifi_cfg.sta.password, "%s", WLAN_PASSWORT);
        quelle = "secrets.h";
    }
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.sta.pmf_cfg.capable = true;
    wifi_cfg.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* WLAN-Stromsparmodus aus: Das Board haengt an einem Netzteil, Strom
     * sparen ist unnoetig - der periodische Modem-Schlaf/Aufwach-Zyklus
     * konkurriert sonst mit dem RGB-Display um die PSRAM-Bandbreite und
     * zeigt sich als gelegentliches Flackern, das mit keiner erkennbaren
     * Aktion im Programm zusammenhaengt. */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_LOGI(TAG, "Verbinde mit WLAN (Zugangsdaten aus %s)...", quelle);
}

bool netz_ist_verbunden(void)
{
    return s_verbunden;
}
