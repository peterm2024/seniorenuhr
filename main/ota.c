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
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

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
 * pruefen braechte nur unnoetigen Datenverkehr.
 *
 * Die Anlaufzeit lag anfangs bei 3 Minuten. Der Boot ist nach rund 20
 * Sekunden durch (Log: "Start: Uhr laeuft"), eine Minute laesst also
 * reichlich Luft - und drei Minuten hiessen bei jedem Neustart drei Minuten
 * Warten, bevor sich ueberhaupt zeigt, ob das Update-Symbol kommt. */
#define OTA_ERSTE_PRUEFUNG_MS (60 * 1000)
#define OTA_INTERVALL_MS      (30 * 60 * 1000)
/* Wie oft der Task nachsieht, ob im Einstellungen-Menue ein Update
 * angestossen wurde - kurz genug, dass der Tipp sich sofort anfuehlt. */
#define OTA_ANSTOSS_ABFRAGE_MS 1000

static volatile bool s_laeuft = false;
static volatile int s_fortschritt_prozent = -1;
/* Klartext fuer das Fortschrittsfenster. Leer = nichts zu sagen, dann zeigt
 * das Fenster den Prozentbalken. */
static char s_meldung[80] = "";
/* Wie lange eine Abschlussmeldung stehen bleibt, bevor sich das Fenster
 * schliesst - lange genug zum Lesen, ohne im Weg zu stehen. */
#define OTA_MELDUNG_STEHENZEIT_MS (12 * 1000)
/* Bewaehrungsfrist fuer eine frisch eingespielte Version: so lange darf sie
 * brauchen, um WLAN und Kalender zum Laufen zu bringen. Grosszuegig, weil ein
 * langsamer Router oder ein zaeher Kalender-Abruf kein Rueckfallgrund sein
 * soll - aber endlich, damit ein dauerhaft unbrauchbarer Stand nicht ewig
 * bestehen bleibt. */
#define OTA_BEWAEHRUNG_MS (10 * 60 * 1000)
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
const char *ota_meldung(void) { return s_meldung; }
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

    ESP_LOGI(TAG, "Frisch per OTA eingespielt - warte bis zu %d Minuten auf WLAN + Kalender, "
                  "bevor die Version bestaetigt wird", OTA_BEWAEHRUNG_MS / 60000);

    int64_t gewartet_ms = 0;
    while (!(netz_ist_verbunden() && kalender_anzeige_version() != 0)) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        gewartet_ms += 2000;
        if (gewartet_ms >= OTA_BEWAEHRUNG_MS) {
            /* Hier lag die eigentliche Luecke: vorher wurde ohne Zeitgrenze
             * gewartet. Eine Firmware, die zwar startet, aber kein Netz
             * bekommt, wurde damit weder bestaetigt NOCH zurueckgenommen -
             * das Geraet lief einfach dauerhaft offline weiter. Genau das ist
             * beim ersten echten Update passiert: die Release-Binary enthaelt
             * nur Platzhalter statt der Zugangsdaten aus secrets.h, und ohne
             * Netz kann auch nie wieder ein Update nachkommen. Peter konnte
             * am Geraet von Hand zurueckschalten - seine Eltern koennten das
             * nicht.
             *
             * Diese Funktion startet das Geraet neu; der Bootloader nimmt
             * dabei automatisch die vorherige, bewaehrte Version. */
            ESP_LOGE(TAG, "Neue Version binnen %d Minuten nicht brauchbar (WLAN verbunden: %d, "
                          "Kalender geladen: %d) - Rueckkehr zur vorherigen Version",
                     OTA_BEWAEHRUNG_MS / 60000, netz_ist_verbunden(),
                     kalender_anzeige_version() != 0);
            vTaskDelay(pdMS_TO_TICKS(500)); /* Log noch rausschreiben lassen */
            esp_ota_mark_app_invalid_rollback_and_reboot();
            return; /* unerreichbar */
        }
    }

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
 * unangekuendigt bei seinen Eltern landet.
 *
 * Rueckgabe: true, sobald die Version des neuen Images gelesen werden konnte
 * - also ein belastbares Urteil vorliegt. false heisst "keine Aussage
 * moeglich" (Verbindung gescheitert) und ist der Anlass fuer einen erneuten
 * Anlauf, siehe pruefung_mit_wiederholung. */
/* Diagnose vor jedem Verbindungsversuch. Anlass: das Geraet meldete
 * "Failed to open new connection in specified timeout" fuer github.com,
 * waehrend derselbe Abruf vom PC im selben Netz mit HTTP 200 durchlief und
 * der Kalender-Download (ebenfalls HTTPS) 80 Sekunden zuvor geklappt hatte.
 * Ohne diese beiden Zahlen bleibt unentscheidbar, ob es an der Namens-
 * aufloesung, am fehlenden Speicher oder an der Gegenstelle liegt. */
static void verbindungsdiagnose(const char *host)
{
    struct addrinfo hinweise = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
    struct addrinfo *ergebnis = NULL;
    int64_t start_us = esp_timer_get_time();
    int fehler = getaddrinfo(host, "443", &hinweise, &ergebnis);
    int64_t dauer_ms = (esp_timer_get_time() - start_us) / 1000;

    if (fehler != 0 || ergebnis == NULL) {
        ESP_LOGW(TAG, "Diagnose: Namensaufloesung fuer %s fehlgeschlagen (%d) nach %lld ms",
                 host, fehler, dauer_ms);
    } else {
        char ip[16] = "?";
        struct sockaddr_in *adr = (struct sockaddr_in *)ergebnis->ai_addr;
        inet_ntoa_r(adr->sin_addr, ip, sizeof ip);
        ESP_LOGI(TAG, "Diagnose: %s -> %s (%lld ms)", host, ip, dauer_ms);
        freeaddrinfo(ergebnis);
    }

    ESP_LOGI(TAG, "Diagnose: frei intern %u Byte (groesster Block %u), PSRAM %u Byte",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    /* VORUEBERGEHEND (Fehlersuche 08.08.2026): ungenutzte Stack-Reserve der
     * langlebigen Tasks. Anlass: das Einstellungen-Menue laesst sich nicht
     * mehr oeffnen, weil im internen SRAM kein 8-KB-Block mehr frei ist -
     * waehrend mehrere Tasks ihren Stack dauerhaft aus genau diesem Topf
     * halten. Wer davon reichlich Luft hat, kann abgeben. Besonders im
     * Verdacht: der Kalender-Task mit 16 KB, dimensioniert BEVOR sein
     * grosses termine[32]-Array in den PSRAM wanderte (FALLSTRICKE #26). */
    static const char *const beobachtete_tasks[] = { "kalender", "ota", "httpd", "wifi_neuscan", "LVGL" };
    for (size_t i = 0; i < sizeof(beobachtete_tasks) / sizeof(beobachtete_tasks[0]); i++) {
        TaskHandle_t t = xTaskGetHandle(beobachtete_tasks[i]);
        if (t)
            ESP_LOGI(TAG, "Diagnose: Task %-12s ungenutzte Stack-Reserve %u Byte",
                     beobachtete_tasks[i],
                     (unsigned)(uxTaskGetStackHighWaterMark(t) * sizeof(StackType_t)));
    }

    /* Empfangsstaerke: der Verdacht ist, dass die grosse Zertifikatskette
     * von GitHub auf einer schwachen Funkstrecke haengen bleibt, waehrend
     * kleine Abrufe (DNS, der 3 KB grosse Kalender) durchkommen. Beim Boot
     * meldete das WLAN -68 dBm - grenzwertig genug, um das zu pruefen. */
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
        ESP_LOGI(TAG, "Diagnose: WLAN %s, RSSI %d dBm, Kanal %d", ap.ssid, ap.rssi, ap.primary);
}

static bool ota_durchlauf(bool installieren, const char *version)
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

    verbindungsdiagnose("github.com");

    esp_http_client_config_t http_cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        /* 15 s reichten nicht: der Verbindungsaufbau lief mehrfach genau
         * hinein ("Failed to open new connection in specified timeout"),
         * waehrend derselbe Abruf zu anderen Zeitpunkten durchkam. Ein
         * Hintergrundabruf hat es nicht eilig - lieber warten als aufgeben. */
        .timeout_ms = 30000,
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
        return false;
    }

    esp_app_desc_t neues_image;
    if (esp_https_ota_get_img_desc(handle, &neues_image) != ESP_OK) {
        ESP_LOGW(TAG, "Konnte Versionsinfo des neuen Images nicht lesen - abgebrochen");
        esp_https_ota_abort(handle);
        return false;
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
        return true;
    }

    if (!installieren) {
        ESP_LOGI(TAG, "Neue Version verfuegbar: %s (laufend: %s) - wartet auf Bestaetigung im Einstellungen-Menue",
                 neues_image.version, laufende_version);
        esp_https_ota_abort(handle);
        return true;
    }

    ESP_LOGI(TAG, "Neue Version gefunden: %s -> %s - lade herunter", laufende_version, neues_image.version);
    /* s_laeuft/s_meldung gehoeren dem Aufrufer (installation_mit_wiederholung)
     * und stehen bereits seit dem Tastendruck - hier nur noch den Balken vom
     * Text auf Prozente umschalten. */
    s_meldung[0] = '\0';
    s_fortschritt_prozent = -1;

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

    if (err != ESP_OK || !vollstaendig || abschluss_err != ESP_OK) {
        ESP_LOGW(TAG, "Update fehlgeschlagen (perform=%s, vollstaendig=%d, finish=%s) - bleibe auf %s",
                 esp_err_to_name(err), vollstaendig, esp_err_to_name(abschluss_err), laufende_version);
        return true; /* Urteil stand fest, nur das Einspielen scheiterte */
    }

    ESP_LOGI(TAG, "Update erfolgreich (%s) - Neustart in 3s", neues_image.version);
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    return true;
}

/* Der Abruf bei GitHub ist gelegentlich flatterhaft - live beobachtet:
 * derselbe Aufruf lieferte einmal sauber die Versionsinfo und wenige Minuten
 * spaeter "Complete headers were not received", die Verbindung wurde also vor
 * den Antwortkoepfen geschlossen. Ohne Wiederholung kostete ein einzelner
 * solcher Aussetzer volle 30 Minuten ohne Update-Symbol; die Versionsliste
 * kam im selben Durchlauf durch, es lag also nicht am WLAN. */
#define OTA_PRUEFUNG_VERSUCHE 3
#define OTA_PRUEFUNG_PAUSE_MS (20 * 1000)

static void pruefung_mit_wiederholung(void)
{
    for (int versuch = 1; versuch <= OTA_PRUEFUNG_VERSUCHE; versuch++) {
        if (ota_durchlauf(false, NULL))
            return;
        if (versuch < OTA_PRUEFUNG_VERSUCHE) {
            ESP_LOGI(TAG, "Update-Pruefung ohne Ergebnis (Versuch %d von %d) - neuer Anlauf in %d s",
                     versuch, OTA_PRUEFUNG_VERSUCHE, OTA_PRUEFUNG_PAUSE_MS / 1000);
            vTaskDelay(pdMS_TO_TICKS(OTA_PRUEFUNG_PAUSE_MS));
        }
    }
    ESP_LOGW(TAG, "Update-Pruefung nach %d Versuchen aufgegeben - naechster Anlauf in %d Minuten",
             OTA_PRUEFUNG_VERSUCHE, OTA_INTERVALL_MS / 60000);
}

/* Die vom Benutzer ANGESTOSSENE Installation. Zwei Unterschiede zur
 * Hintergrund-Pruefung, beide aus einer Beobachtung am Geraet:
 *
 * 1. Sie wiederholt sich ebenfalls. Vorher lief sie genau einmal - traf sie
 *    einen der GitHub-Aussetzer, war der Vorgang still vorbei.
 * 2. `s_laeuft` (und damit das Fortschrittsfenster) steht ab dem ERSTEN
 *    Moment, nicht erst wenn der Download tatsaechlich anlaeuft. Genau daran
 *    wirkte der Knopf tot: bei einer gescheiterten Verbindung wurde
 *    `s_laeuft` nie gesetzt, es erschien also nie irgendetwas - Peters
 *    Rueckmeldung war woertlich "der Update Button funktioniert nicht",
 *    obwohl er im Log sauber ausgeloest hatte.
 *
 * Ein Knopf, der schweigend scheitert, ist schlimmer als einer, der eine
 * Fehlermeldung zeigt - erst recht bei Nutzern, die nicht ins Log sehen. */
static void installation_mit_wiederholung(const char *version)
{
    s_laeuft = true;
    s_fortschritt_prozent = -1;

    for (int versuch = 1; versuch <= OTA_PRUEFUNG_VERSUCHE; versuch++) {
        snprintf(s_meldung, sizeof s_meldung, "Verbinde mit GitHub (Versuch %d von %d)...",
                 versuch, OTA_PRUEFUNG_VERSUCHE);
        if (ota_durchlauf(true, version)) {
            /* Bei Erfolg startet das Geraet im Durchlauf selbst neu; hierher
             * kommt man nur, wenn das Einspielen scheiterte. */
            snprintf(s_meldung, sizeof s_meldung, "Update fehlgeschlagen - Geraet laeuft unveraendert weiter");
            break;
        }
        if (versuch < OTA_PRUEFUNG_VERSUCHE)
            vTaskDelay(pdMS_TO_TICKS(OTA_PRUEFUNG_PAUSE_MS));
        else
            snprintf(s_meldung, sizeof s_meldung, "Keine Verbindung zu GitHub - bitte spaeter erneut versuchen");
    }

    ESP_LOGW(TAG, "Installation beendet: %s", s_meldung);
    /* Meldung noch einen Moment stehen lassen, sonst verschwindet das
     * Fenster schneller, als man es lesen kann. */
    vTaskDelay(pdMS_TO_TICKS(OTA_MELDUNG_STEHENZEIT_MS));
    s_meldung[0] = '\0';
    s_laeuft = false;
}

static void ota_task(void *arg)
{
    (void)arg;

    rollback_bestaetigen_falls_noetig();

    vTaskDelay(pdMS_TO_TICKS(OTA_ERSTE_PRUEFUNG_MS));
    pruefung_mit_wiederholung(); /* erste Pruefung, nur melden */
    ota_versionen_abfragen();    /* Auswahlliste fuers Einstellungen-Menue fuellen */

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
            installation_mit_wiederholung(version);
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
            pruefung_mit_wiederholung();
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
