#include "ota.h"
#include "kalender_anzeige.h"
#include "netz.h"

#include <string.h>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ota";

/* Stack-Groesse des Hintergrund-Tasks. Der eigentliche OTA-Code selbst
 * braucht kaum Stack (kein grosser lokaler Puffer wie z. B. bei
 * kalender_anzeige.c's ics_termin_t[32], siehe FALLSTRICKE #8) - den
 * Loewenanteil braucht der HTTPS/TLS-Handshake in esp_https_ota/mbedtls.
 * MUSS aus dem internen SRAM kommen (Standard von xTaskCreate) - ein
 * PSRAM-Stack (per xTaskCreateStatic) wurde live getestet und stuerzte
 * sofort beim ersten Flash-Zugriff ab: "assert failed:
 * esp_cache_freeze_caches_disable_interrupts ...
 * s_task_stack_is_sane_when_cache_frozen()" - ein Task, der (wie dieser,
 * beim Schreiben der neuen Firmware) Flash beschreibt, braucht seinen
 * Stack zwingend im internen SRAM, weil der Cache dafuer kurz eingefroren
 * wird und PSRAM darueber gar nicht mehr erreichbar ist. */
#define OTA_TASK_STACK_BYTES 8192

/* Wird das Erzeugen des Tasks direkt am Ende von app_main() versucht, ist
 * dessen eigener 16-KB-Stack (CONFIG_ESP_MAIN_TASK_STACK_SIZE) noch nicht
 * freigegeben, dazu kommt Fragmentierung durch die WLAN-/TLS-Aktivitaet
 * waehrend des Bootens - live beobachtet: 13311 Byte frei, trotzdem schlug
 * die Allokation fuer 8192 Byte fehl. Ein kurzer Aufschub reicht: sobald
 * app_main() zurueckkehrt, gibt der Idle-Task dessen Stack frei, und die
 * Fragmentierung durch den Boot-Trubel hat sich gelegt. */
#define OTA_START_VERZOEGERUNG_US (5 * 1000 * 1000)

/* GitHub liefert unter ".../releases/latest/download/<Datei>" immer das
 * Asset des NEUESTEN Releases - eine feste URL genuegt, keine API, kein
 * JSON-Parser, kein Rate-Limit. Wird vom Workflow .github/workflows/
 * release.yml bei jedem "git tag vX.Y.Z" neu befuellt. */
#define OTA_FIRMWARE_URL "https://github.com/peterm2024/seniorenuhr/releases/latest/download/seniorenuhr.bin"

/* Erste Pruefung erst nach etwas Anlaufzeit (Boot nicht zusaetzlich
 * belasten, WLAN/Kalender sollen zuerst stehen), danach alle 30 Minuten -
 * ein neues Release ist kein staendiges Ereignis, taeglich mehrfach zu
 * pruefen braechte nur unnoetigen Datenverkehr. */
#define OTA_ERSTE_PRUEFUNG_MS (3 * 60 * 1000)
#define OTA_INTERVALL_MS      (30 * 60 * 1000)

static volatile bool s_laeuft = false;
static volatile int s_fortschritt_prozent = -1;

bool ota_laeuft(void) { return s_laeuft; }
int ota_fortschritt_prozent(void) { return s_fortschritt_prozent; }

/* Laeuft die App gerade zum ersten Mal nach einem OTA-Update (Zustand
 * "pending verify", siehe CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE in
 * sdkconfig.defaults), wartet hier BIS das Geraet nachweislich brauchbar
 * ist - erst dann wird die Version bestaetigt und der automatische
 * Rollback-Schutz deaktiviert. Ein frischer USB-Flash (Zustand bereits
 * ESP_OTA_IMG_VALID) braucht das nicht und faellt sofort durch. Blockiert
 * absichtlich nur DIESEN Hintergrund-Task, nicht den Boot-Ablauf. */
static void rollback_bestaetigen_falls_noetig(void)
{
    const esp_partition_t *laufend = esp_ota_get_running_partition();
    esp_ota_img_states_t zustand;
    if (esp_ota_get_state_partition(laufend, &zustand) != ESP_OK)
        return;
    if (zustand != ESP_OTA_IMG_PENDING_VERIFY)
        return;

    ESP_LOGI(TAG, "Frisch per OTA eingespielt - warte auf WLAN + Kalender, bevor die Version bestaetigt wird");
    while (!(netz_ist_verbunden() && kalender_anzeige_version() != 0))
        vTaskDelay(pdMS_TO_TICKS(2000));

    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK)
        ESP_LOGI(TAG, "Update bestaetigt (WLAN verbunden, Kalender geladen) - Rollback-Schutz deaktiviert");
    else
        ESP_LOGW(TAG, "Konnte Update nicht bestaetigen: %s", esp_err_to_name(err));
}

/* Ein Durchlauf: verbindet, vergleicht die Version im Bildkopf des neuen
 * Images (esp_https_ota_get_img_desc, noch VOR dem eigentlichen Download)
 * gegen die laufende Version - bei Gleichstand wird abgebrochen, ohne
 * ueberhaupt Flash zu beschreiben. Erst bei einer abweichenden Version
 * folgt der eigentliche Download samt Fortschrittsmeldung. */
static void ota_pruefen_und_aktualisieren(void)
{
    esp_http_client_config_t http_cfg = {
        .url = OTA_FIRMWARE_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .buffer_size = 2048,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t ota_cfg = { .http_config = &http_cfg };

    esp_https_ota_handle_t handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_cfg, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Update-Pruefung fehlgeschlagen: %s", esp_err_to_name(err));
        return;
    }

    esp_app_desc_t neues_image;
    if (esp_https_ota_get_img_desc(handle, &neues_image) != ESP_OK) {
        ESP_LOGW(TAG, "Konnte Versionsinfo des neuen Images nicht lesen - abgebrochen");
        esp_https_ota_abort(handle);
        return;
    }

    const char *laufende_version = esp_app_get_description()->version;
    if (strncmp(neues_image.version, laufende_version, sizeof neues_image.version) == 0) {
        ESP_LOGI(TAG, "Keine neue Version (laufend: %s)", laufende_version);
        esp_https_ota_abort(handle);
        return;
    }

    ESP_LOGI(TAG, "Neue Version gefunden: %s -> %s - lade herunter", laufende_version, neues_image.version);
    s_fortschritt_prozent = -1;
    s_laeuft = true;

    int bildgroesse = esp_https_ota_get_image_size(handle);
    for (;;) {
        err = esp_https_ota_perform(handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS)
            break;
        if (bildgroesse > 0) {
            int gelesen = esp_https_ota_get_image_len_read(handle);
            s_fortschritt_prozent = (gelesen * 100) / bildgroesse;
        }
    }

    bool vollstaendig = esp_https_ota_is_complete_data_received(handle);
    esp_err_t abschluss_err = esp_https_ota_finish(handle);
    s_laeuft = false;

    if (err != ESP_OK || !vollstaendig || abschluss_err != ESP_OK) {
        ESP_LOGW(TAG, "Update fehlgeschlagen (perform=%s, vollstaendig=%d, finish=%s) - bleibe auf %s",
                 esp_err_to_name(err), vollstaendig, esp_err_to_name(abschluss_err), laufende_version);
        return;
    }

    ESP_LOGI(TAG, "Update erfolgreich (%s) - Neustart in 3s", neues_image.version);
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
}

static void ota_task(void *arg)
{
    (void)arg;

    rollback_bestaetigen_falls_noetig();

    vTaskDelay(pdMS_TO_TICKS(OTA_ERSTE_PRUEFUNG_MS));
    for (;;) {
        ota_pruefen_und_aktualisieren();
        vTaskDelay(pdMS_TO_TICKS(OTA_INTERVALL_MS));
    }
}

static void ota_start_verzoegert_cb(void *arg)
{
    (void)arg;
    if (xTaskCreate(ota_task, "ota", OTA_TASK_STACK_BYTES, NULL, 3, NULL) != pdPASS)
        ESP_LOGW(TAG, "OTA-Task konnte nicht gestartet werden - Updates bleiben bis zum naechsten Neustart aus");
}

void ota_starten(void)
{
    const esp_timer_create_args_t timer_cfg = {
        .callback = ota_start_verzoegert_cb,
        .name = "ota_start",
    };
    esp_timer_handle_t timer;
    if (esp_timer_create(&timer_cfg, &timer) != ESP_OK) {
        ESP_LOGW(TAG, "Konnte OTA-Start nicht verzoegern - versuche sofort");
        ota_start_verzoegert_cb(NULL);
        return;
    }
    esp_timer_start_once(timer, OTA_START_VERZOEGERUNG_US);
}
