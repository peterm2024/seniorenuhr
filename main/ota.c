#include "ota.h"
#include "kalender_anzeige.h"
#include "netz.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
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

/* Heruntergeladen wird aus einem SEPARATEN, OEFFENTLICHEN Repo, nicht aus
 * dem privaten Quellcode-Repo: Release-Assets eines privaten Repos lassen
 * sich nur mit Zugangstoken laden, und ein Token in der Firmware waere aus
 * mehreren Gruenden schlecht (steckt im Binary, laeuft ab). Das
 * Download-Repo enthaelt nur fertige Binaries mit Platzhalter-Zugangsdaten
 * (siehe .github/workflows/release.yml), also nichts Schuetzenswertes.
 *
 * "releases/latest/download/<Datei>" liefert immer das Asset des NEUESTEN
 * Releases - fuer die Pruefung genuegt diese feste URL ohne API-Aufruf. */
#define OTA_REPO "peterm2024/seniorenuhr-firmware"
#define OTA_FIRMWARE_URL "https://github.com/" OTA_REPO "/releases/latest/download/seniorenuhr.bin"

/* Erste Pruefung erst nach etwas Anlaufzeit (Boot nicht zusaetzlich
 * belasten, WLAN/Kalender sollen zuerst stehen), danach alle 30 Minuten -
 * ein neues Release ist kein staendiges Ereignis, taeglich mehrfach zu
 * pruefen braechte nur unnoetigen Datenverkehr. */
#define OTA_ERSTE_PRUEFUNG_MS (3 * 60 * 1000)
#define OTA_INTERVALL_MS      (30 * 60 * 1000)
/* Wie oft der Task nachsieht, ob im Einstellungen-Menue ein Update
 * angestossen wurde - kurz genug, dass der Tipp sich sofort anfuehlt. */
#define OTA_ANSTOSS_ABFRAGE_MS 1000

static volatile bool s_laeuft = false;
static volatile int s_fortschritt_prozent = -1;
/* Ergebnis der letzten Pruefung. Installiert wird NIE von selbst (Peters
 * Entscheidung) - das Geraet meldet nur, dass etwas bereitsteht, und wartet
 * auf den Update-Button im Einstellungen-Menue. */
static volatile bool s_update_verfuegbar = false;
static char s_verfuegbare_version[32] = "";
/* Wird vom Einstellungen-Menue gesetzt und vom OTA-Task abgeholt - der
 * Download darf nicht im LVGL-Task laufen (Task-Watchdog, FALLSTRICKE #16
 * und #19). */
static volatile bool s_installation_gewuenscht = false;
/* Gezielt gewaehlte Version aus der Auswahlliste (leer = "neueste"). Nur
 * gueltig, solange s_installation_gewuenscht gesetzt ist. */
static char s_gewuenschte_version[OTA_VERSION_MAX] = "";

/* Zwischenspeicher der zuletzt abgefragten Release-Liste. Wird nur beim
 * Oeffnen des Einstellungen-Menues aktualisiert, nicht periodisch - die
 * Liste aendert sich hoechstens alle paar Wochen. */
static char s_versionen[OTA_VERSIONEN_MAX][OTA_VERSION_MAX];
static int s_versionen_anzahl = 0;
/* Vom Einstellungen-Menue gesetzt, vom Hintergrund-Task abgeholt. Die
 * API-Abfrage darf NIE im LVGL-Task laufen - sie dauert Sekunden und wuerde
 * den Task-Watchdog ausloesen (FALLSTRICKE #16/#19). */
static volatile bool s_versionen_abfrage_gewuenscht = false;

bool ota_laeuft(void) { return s_laeuft; }
int ota_fortschritt_prozent(void) { return s_fortschritt_prozent; }
bool ota_update_verfuegbar(void) { return s_update_verfuegbar; }
const char *ota_verfuegbare_version(void) { return s_verfuegbare_version; }
const char *ota_laufende_version(void) { return esp_app_get_description()->version; }
void ota_installation_anstossen(void) { s_installation_gewuenscht = true; }

/* Die zweite App-Partition haelt genau eine weitere Version: die zuvor
 * laufende (bzw. die zuletzt heruntergeladene). Mehr als zwei sind
 * prinzipbedingt nicht moeglich - das Board hat nur ota_0 und ota_1 (siehe
 * partitions.csv). Eine Versions-Auswahlliste kann es deshalb nicht geben,
 * nur dieses Zurueckschalten. */
bool ota_vorherige_version(char *puffer, size_t puffer_groesse)
{
    const esp_partition_t *andere = esp_ota_get_next_update_partition(NULL);
    if (!andere)
        return false;

    esp_app_desc_t beschreibung;
    if (esp_ota_get_partition_description(andere, &beschreibung) != ESP_OK)
        return false; /* Slot noch leer (nie ein Update eingespielt) */

    /* Als ungueltig markierte Images (z. B. nach automatischem Rollback)
     * nicht anbieten - dorthin zurueckzuschalten wuerde nur erneut
     * fehlschlagen. */
    esp_ota_img_states_t zustand;
    if (esp_ota_get_state_partition(andere, &zustand) == ESP_OK &&
        (zustand == ESP_OTA_IMG_INVALID || zustand == ESP_OTA_IMG_ABORTED))
        return false;

    snprintf(puffer, puffer_groesse, "%s", beschreibung.version);
    return true;
}

/* Holt die Release-Liste des Download-Repos ueber die GitHub-API und
 * uebernimmt die Versionsnamen (tag_name) in s_versionen. GitHub liefert
 * sie bereits nach Datum absteigend, die neueste steht also vorn.
 *
 * Bewusst nur auf ausdrueckliche Anfrage (Oeffnen des Einstellungen-Menues)
 * statt periodisch: die Liste aendert sich hoechstens alle paar Wochen, und
 * die API erlaubt unangemeldet nur 60 Abfragen pro Stunde.
 *
 * Antwort landet komplett im PSRAM - bei 10 Releases sind das einige
 * Kilobyte JSON, die im knappen internen SRAM nichts zu suchen haben
 * (FALLSTRICKE #20). */
#define OTA_API_ANTWORT_MAX (24 * 1024)

int ota_versionen_abfragen(void)
{
    s_versionen_anzahl = 0;

    char url[160];
    snprintf(url, sizeof url,
             "https://api.github.com/repos/" OTA_REPO "/releases?per_page=%d", OTA_VERSIONEN_MAX);

    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        /* Auch die API antwortet mit reichlich Headern (Rate-Limit-,
         * Sicherheits- und Cache-Angaben) - dieselbe Falle wie oben. */
        .buffer_size = 4096,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
        return 0;
    /* GitHub verlangt einen User-Agent, sonst wird die Anfrage abgewiesen. */
    esp_http_client_set_header(client, "User-Agent", "seniorenuhr");
    esp_http_client_set_header(client, "Accept", "application/vnd.github+json");

    char *antwort = heap_caps_malloc(OTA_API_ANTWORT_MAX, MALLOC_CAP_SPIRAM);
    if (!antwort) {
        ESP_LOGW(TAG, "Kein PSRAM fuer die Release-Liste");
        esp_http_client_cleanup(client);
        return 0;
    }

    int gelesen = 0;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK) {
        esp_http_client_fetch_headers(client);
        int n;
        while (gelesen < OTA_API_ANTWORT_MAX - 1 &&
               (n = esp_http_client_read(client, antwort + gelesen,
                                          OTA_API_ANTWORT_MAX - 1 - gelesen)) > 0)
            gelesen += n;
        esp_http_client_close(client);
    } else {
        ESP_LOGW(TAG, "Release-Liste nicht erreichbar: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);

    if (gelesen <= 0) {
        heap_caps_free(antwort);
        return 0;
    }
    antwort[gelesen] = '\0';

    cJSON *wurzel = cJSON_Parse(antwort);
    heap_caps_free(antwort);
    if (!wurzel || !cJSON_IsArray(wurzel)) {
        ESP_LOGW(TAG, "Release-Liste unlesbar");
        cJSON_Delete(wurzel);
        return 0;
    }

    cJSON *eintrag;
    cJSON_ArrayForEach(eintrag, wurzel) {
        if (s_versionen_anzahl >= OTA_VERSIONEN_MAX)
            break;
        /* Entwuerfe und Vorabversionen ueberspringen - auf dem Geraet
         * seiner Eltern hat nur Fertiges etwas verloren. */
        if (cJSON_IsTrue(cJSON_GetObjectItem(eintrag, "draft")) ||
            cJSON_IsTrue(cJSON_GetObjectItem(eintrag, "prerelease")))
            continue;
        cJSON *tag = cJSON_GetObjectItem(eintrag, "tag_name");
        if (!cJSON_IsString(tag) || !tag->valuestring[0])
            continue;
        snprintf(s_versionen[s_versionen_anzahl], OTA_VERSION_MAX, "%s", tag->valuestring);
        s_versionen_anzahl++;
    }
    cJSON_Delete(wurzel);

    ESP_LOGI(TAG, "%d Version(en) im Download-Repo gefunden", s_versionen_anzahl);
    return s_versionen_anzahl;
}

int ota_versionen_anzahl(void) { return s_versionen_anzahl; }
void ota_versionen_auffrischen(void) { s_versionen_abfrage_gewuenscht = true; }

const char *ota_version_name(int index)
{
    if (index < 0 || index >= s_versionen_anzahl)
        return "";
    return s_versionen[index];
}

void ota_version_installieren(const char *version)
{
    snprintf(s_gewuenschte_version, sizeof s_gewuenschte_version, "%s", version ? version : "");
    s_installation_gewuenscht = true;
}

esp_err_t ota_auf_vorherige_version_wechseln(void)
{
    const esp_partition_t *andere = esp_ota_get_next_update_partition(NULL);
    if (!andere)
        return ESP_ERR_NOT_FOUND;

    esp_app_desc_t beschreibung;
    if (esp_ota_get_partition_description(andere, &beschreibung) != ESP_OK)
        return ESP_ERR_NOT_FOUND;

    esp_err_t err = esp_ota_set_boot_partition(andere);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Konnte nicht auf %s zurueckschalten: %s",
                 beschreibung.version, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Zurueck auf Version %s - Neustart", beschreibung.version);
    return ESP_OK;
}

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
 * ueberhaupt Flash zu beschreiben.
 *
 * `installieren` entscheidet, was danach passiert: false = nur merken, dass
 * etwas bereitsteht (fuer das Update-Symbol auf dem Hauptbildschirm), true =
 * herunterladen und einspielen. Die Trennung ist Peters Entscheidung -
 * automatisch installiert wird nichts mehr, damit ein Update nie
 * unangekuendigt bei seinen Eltern landet. */
static void ota_durchlauf(bool installieren, const char *version)
{
    /* Ohne Versionsangabe die "latest"-URL, sonst gezielt das Asset des
     * gewaehlten Tags - so laesst sich auch eine AELTERE Version wieder
     * einspielen (Peters Fall: Update gefaellt nicht, spaeter aber doch ein
     * Feature daraus). Moeglich ist das nur, weil Anti-Rollback bewusst
     * ausgeschaltet bleibt (CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK, siehe
     * sdkconfig.defaults) - waere es aktiv, wuerde der Bootloader jede
     * aeltere Version abweisen. */
    char url[200];
    if (version && version[0])
        snprintf(url, sizeof url,
                 "https://github.com/" OTA_REPO "/releases/download/%s/seniorenuhr.bin", version);
    else
        snprintf(url, sizeof url, "%s", OTA_FIRMWARE_URL);

    esp_http_client_config_t http_cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .buffer_size = 4096,
        /* ENTSCHEIDEND, und leicht zu verwechseln: "Out of buffer" kam beim
         * Download aus dem SENDE-Puffer, nicht aus dem Empfangspuffer.
         * esp_http_client baut damit die Anfragezeile "GET <pfad>?<query>
         * HTTP/1.1" zusammen (esp_http_client.c, esp_http_client_prepare_
         * first_line). GitHub leitet Release-Downloads per 302 auf eine
         * signierte Adresse um, deren Pfad samt Query mehrere hundert
         * Zeichen lang ist - die Vorgabe von 512 Byte reicht dafuer nicht,
         * und ein groesserer Empfangspuffer aendert daran nichts.
         *
         * Aufgefallen ist es erst, seit es ueberhaupt ein Release gibt:
         * vorher kam ein kurzes 404 ohne Weiterleitung zurueck. Der
         * Kalender-Abruf ist nicht betroffen, Google antwortet direkt. */
        .buffer_size_tx = 4096,
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
    bool gleich = strncmp(neues_image.version, laufende_version, sizeof neues_image.version) == 0;

    /* Den "Update verfuegbar"-Zustand nur bei einer PRUEFUNG fortschreiben.
     * Bei einer gezielten Installation (z. B. bewusst eine aeltere Version)
     * sagt der Vergleich nichts darueber aus, ob im Netz etwas Neueres
     * liegt - das Symbol duerfte davon nicht durcheinandergeraten. */
    if (!installieren) {
        s_update_verfuegbar = !gleich;
        if (gleich)
            s_verfuegbare_version[0] = '\0';
        else
            snprintf(s_verfuegbare_version, sizeof s_verfuegbare_version, "%s", neues_image.version);
    }

    if (gleich) {
        ESP_LOGI(TAG, "Version %s laeuft bereits - nichts zu tun", laufende_version);
        esp_https_ota_abort(handle);
        return;
    }

    if (!installieren) {
        ESP_LOGI(TAG, "Neue Version verfuegbar: %s (laufend: %s) - wartet auf Bestaetigung im Einstellungen-Menue",
                 neues_image.version, laufende_version);
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
    ota_durchlauf(false, NULL); /* erste Pruefung, nur melden */
    ota_versionen_abfragen();   /* Auswahlliste fuers Einstellungen-Menue fuellen */

    /* In kurzen Schritten warten statt einmal 30 Minuten am Stueck, damit
     * ein per Einstellungen-Menue angestossenes Update nicht bis zum
     * naechsten Pruefintervall liegen bleibt, sondern binnen Sekunden
     * anlaeuft. */
    int64_t naechste_pruefung_ms = OTA_INTERVALL_MS;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(OTA_ANSTOSS_ABFRAGE_MS));

        if (s_installation_gewuenscht) {
            s_installation_gewuenscht = false;
            /* Kopie ziehen: s_gewuenschte_version koennte waehrend des
             * (langen) Downloads von der Oberflaeche neu gesetzt werden. */
            char version[OTA_VERSION_MAX];
            snprintf(version, sizeof version, "%s", s_gewuenschte_version);
            ota_durchlauf(true, version);
            naechste_pruefung_ms = OTA_INTERVALL_MS;
            continue;
        }

        if (s_versionen_abfrage_gewuenscht) {
            s_versionen_abfrage_gewuenscht = false;
            ota_versionen_abfragen();
            continue;
        }

        naechste_pruefung_ms -= OTA_ANSTOSS_ABFRAGE_MS;
        if (naechste_pruefung_ms <= 0) {
            ota_durchlauf(false, NULL);
            ota_versionen_abfragen();
            naechste_pruefung_ms = OTA_INTERVALL_MS;
        }
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
