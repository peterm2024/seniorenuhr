#include "einstellungen.h"
#include "secrets.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NVS_NAMENSRAUM "einstellungen"

/* Hoechstens alle 60s tatsaechlich ins NVS schreiben - "letzte Anzeige" wird
 * jede Sekunde aus uhr_tick() aktualisiert, ein NVS-Commit pro Sekunde waere
 * unnoetiger Flash-Verschleiss fuer einen Wert, der nur als grober
 * Vorbelegungs-Anhaltspunkt nach einem Stromausfall dient. */
#define LETZTE_ANZEIGE_MIN_ABSTAND_S 60

static const char *TAG = "einstellungen";

static bool s_buzzer_aktiv;
static char s_kalender_url[EINSTELLUNGEN_KALENDER_URL_MAX];
static time_t s_letzte_anzeige;
static time_t s_letzte_anzeige_nvs_stand;
static time_t s_letzte_sync;
static time_t s_letzter_kalender_sync;

void einstellungen_laden(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    nvs_handle_t h;
    if (nvs_open(NVS_NAMENSRAUM, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "Noch keine gespeicherten Einstellungen - Standardwerte");
        return;
    }

    uint8_t u8 = 0;
    if (nvs_get_u8(h, "buzzer", &u8) == ESP_OK)
        s_buzzer_aktiv = u8 != 0;

    size_t laenge = sizeof s_kalender_url;
    if (nvs_get_str(h, "kal_url", s_kalender_url, &laenge) != ESP_OK)
        s_kalender_url[0] = '\0';

    int64_t i64 = 0;
    if (nvs_get_i64(h, "let_anz", &i64) == ESP_OK) {
        s_letzte_anzeige = (time_t)i64;
        s_letzte_anzeige_nvs_stand = s_letzte_anzeige;
    }
    if (nvs_get_i64(h, "let_sync", &i64) == ESP_OK)
        s_letzte_sync = (time_t)i64;
    if (nvs_get_i64(h, "let_kal", &i64) == ESP_OK)
        s_letzter_kalender_sync = (time_t)i64;

    nvs_close(h);
    ESP_LOGI(TAG, "Einstellungen geladen (Buzzer=%d, Kalender-Override=%s)",
             s_buzzer_aktiv, s_kalender_url[0] ? "ja" : "nein");
}

bool einstellungen_buzzer_aktiv(void) { return s_buzzer_aktiv; }

void einstellungen_buzzer_aktiv_setzen(bool an)
{
    s_buzzer_aktiv = an;
    nvs_handle_t h;
    if (nvs_open(NVS_NAMENSRAUM, NVS_READWRITE, &h) != ESP_OK)
        return;
    nvs_set_u8(h, "buzzer", an ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

void einstellungen_kalender_url_effektiv(char *puffer, size_t puffer_groesse)
{
    if (s_kalender_url[0])
        snprintf(puffer, puffer_groesse, "%s", s_kalender_url);
    else
        snprintf(puffer, puffer_groesse, "%s", KALENDER_ICS_URL);
}

void einstellungen_kalender_url_setzen(const char *url)
{
    snprintf(s_kalender_url, sizeof s_kalender_url, "%s", url);
    nvs_handle_t h;
    if (nvs_open(NVS_NAMENSRAUM, NVS_READWRITE, &h) != ESP_OK)
        return;
    nvs_set_str(h, "kal_url", s_kalender_url);
    nvs_commit(h);
    nvs_close(h);
}

time_t einstellungen_letzte_anzeige(void) { return s_letzte_anzeige; }

void einstellungen_letzte_anzeige_setzen(time_t zeitstempel)
{
    s_letzte_anzeige = zeitstempel;
    if (zeitstempel - s_letzte_anzeige_nvs_stand < LETZTE_ANZEIGE_MIN_ABSTAND_S)
        return;
    s_letzte_anzeige_nvs_stand = zeitstempel;

    nvs_handle_t h;
    if (nvs_open(NVS_NAMENSRAUM, NVS_READWRITE, &h) != ESP_OK)
        return;
    nvs_set_i64(h, "let_anz", (int64_t)zeitstempel);
    nvs_commit(h);
    nvs_close(h);
}

time_t einstellungen_letzte_sync(void) { return s_letzte_sync; }

void einstellungen_letzte_sync_setzen(time_t zeitstempel)
{
    s_letzte_sync = zeitstempel;
    nvs_handle_t h;
    if (nvs_open(NVS_NAMENSRAUM, NVS_READWRITE, &h) != ESP_OK)
        return;
    nvs_set_i64(h, "let_sync", (int64_t)zeitstempel);
    nvs_commit(h);
    nvs_close(h);
}

time_t einstellungen_letzter_kalender_sync(void) { return s_letzter_kalender_sync; }

void einstellungen_letzter_kalender_sync_setzen(time_t zeitstempel)
{
    s_letzter_kalender_sync = zeitstempel;
    nvs_handle_t h;
    if (nvs_open(NVS_NAMENSRAUM, NVS_READWRITE, &h) != ESP_OK)
        return;
    nvs_set_i64(h, "let_kal", (int64_t)zeitstempel);
    nvs_commit(h);
    nvs_close(h);
}
