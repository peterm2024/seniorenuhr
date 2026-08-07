#include "kalender_anzeige.h"
#include "einstellungen.h"
#include "kalender_holen.h"
#include "kalender_speicher.h"
#include "netz.h"
#include "zeit.h"
#include "ics_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "kalender_anzeige";

#define ABRUF_INTERVALL_US ((int64_t)15 * 60 * 1000000)
#define ABRUF_RETRY_US ((int64_t)30 * 1000000)
#define KEIN_WLAN_RETRY_US ((int64_t)10 * 1000000)
/* Wie oft die Schleife aufwacht, um Mitternachts-Wechsel bzw. eine gerade
 * erst bekannt gewordene Uhrzeit zu pruefen - das eigentliche Abrufintervall
 * (15 Min) wird unabhaengig davon ueber ABRUF_INTERVALL_US gesteuert, ein
 * kurzes Aufwachen kostet also keine zusaetzlichen Netzwerkzugriffe. */
#define TICK_MS 5000

static SemaphoreHandle_t s_mutex;
static volatile uint32_t s_version = 0;
/* true, sobald in dieser Sitzung mindestens ein echter Netz-Download
 * gelungen ist - im Unterschied zu s_version, die auch schon beim reinen
 * Neu-Parsen einer gecachten Datei (z. B. im Offline-Betrieb) steigt. */
static volatile bool s_frisch = false;

/* Zeitpunkt (esp_timer_get_time) des naechsten geplanten Abrufversuchs -
 * modulweit statt lokal in task_funktion(), damit kalender_anzeige_jetzt_pruefen()
 * von aussen einen sofortigen Versuch erzwingen kann (Status-Detail-Fenster,
 * siehe app_main.c). Volatile: geschrieben von der Kalender-Task, gelesen/
 * geschrieben aus dem LVGL-Task heraus. */
static volatile int64_t s_naechster_abruf_us = 0;

/* Rohtext des zuletzt bekannten Kalenders (Cache oder frischer Download) —
 * wird bei Mitternacht erneut geparst, ohne dafuer neu herunterladen zu
 * muessen. */
static char *s_ics_text;
static size_t s_ics_laenge;
static int s_letzter_tag_schluessel = -1; /* JJJJMMTT des letzten Parse-Laufs */

/* Strukturierte Eintraege fuer HEUTE inkl. Bestaetigungsstatus - die UI
 * (app_main.c) baut die eigentlichen Anzeigetexte daraus selbst zusammen. */
static kalender_tag_eintrag_t s_heute_eintraege[KALENDER_EINTRAEGE_MAX];
static int s_heute_anzahl = 0;

static int heute_schluessel(struct tm *ausgabe_lokal)
{
    time_t jetzt = time(NULL);
    localtime_r(&jetzt, ausgabe_lokal);
    return (ausgabe_lokal->tm_year + 1900) * 10000 +
           (ausgabe_lokal->tm_mon + 1) * 100 + ausgabe_lokal->tm_mday;
}

static void eintrag_uebernehmen(kalender_tag_eintrag_t *ziel, const ics_termin_t *quelle)
{
    /* Explizite Praezision (dynamisch aus der Zielgroesse) statt nacktem
     * "%s": ueber einen Zeiger zugegriffene Array-Felder verlieren bei
     * GCCs Format-Truncation-Pruefung ihre bekannte Groesse, wodurch ein
     * sehr grosses Worst-Case-Ergebnis angenommen und
     * -Werror=format-truncation ausgeloest wird. */
    snprintf(ziel->titel, sizeof ziel->titel, "%.*s", (int)sizeof ziel->titel - 1, quelle->titel);
    snprintf(ziel->beschreibung, sizeof ziel->beschreibung, "%.*s",
             (int)sizeof ziel->beschreibung - 1, quelle->beschreibung);
    ziel->stunde = quelle->beginn.stunde;
    ziel->minute = quelle->beginn.minute;
    ziel->hat_ende = quelle->hat_ende;
    ziel->end_stunde = quelle->ende.stunde;
    ziel->end_minute = quelle->ende.minute;
    ziel->ganztags = quelle->ganztags;
    ziel->ist_tablette = quelle->ist_tablette;
    ziel->bestaetigt = false;
}

static void fuer_heute_neu_parsen(void)
{
    if (!s_ics_text)
        return;

    struct tm lokal;
    int schluessel = heute_schluessel(&lokal);
    bool neuer_tag = (schluessel != s_letzter_tag_schluessel);

    /* Aus dem Stack in den PSRAM verlagert (heap_caps_malloc): seit
     * ics_termin_t die Beschreibung traegt (ICS_BESCHREIBUNG_MAX, siehe
     * ics_parser.h) ist ein termine[32]-Array auf dem Stack live nicht mehr
     * sicher - derselbe Fehlerklasse wie FALLSTRICKE #8/#24, nur diesmal
     * durch groessere Eintraege statt mehr Aufrufe. Details FALLSTRICKE #26. */
    ics_termin_t *termine = heap_caps_malloc(32 * sizeof(ics_termin_t), MALLOC_CAP_SPIRAM);
    if (!termine) {
        ESP_LOGE(TAG, "Kein PSRAM fuer Termin-Puffer - Tagesaktualisierung uebersprungen");
        return;
    }

    int n = ics_termine_fuer_tag(s_ics_text, s_ics_laenge,
                                 lokal.tm_year + 1900, lokal.tm_mon + 1, lokal.tm_mday,
                                 termine, 32);
    if (n < 0) {
        ESP_LOGW(TAG, "ICS-Parser meldet ungueltige Argumente");
        heap_caps_free(termine);
        return;
    }

    kalender_tag_eintrag_t neue_eintraege[KALENDER_EINTRAEGE_MAX];
    int neue_anzahl = n < KALENDER_EINTRAEGE_MAX ? n : KALENDER_EINTRAEGE_MAX;
    for (int i = 0; i < neue_anzahl; i++)
        eintrag_uebernehmen(&neue_eintraege[i], &termine[i]);
    heap_caps_free(termine);

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    /* Bestaetigt-Status per Titel-Abgleich aus dem bisherigen Stand
     * uebernehmen - aber nur, wenn es noch derselbe Kalendertag ist. Bei
     * einem Tageswechsel (neuer_tag) starten alle Tabletten unbestaetigt -
     * AUSSER es ist der allererste Parse-Durchlauf seit dem Start
     * (s_letzter_tag_schluessel noch -1): dann koennte ein unerwarteter
     * Neustart mitten am Tag stattgefunden haben, und bereits genommene
     * Tabletten sollen nicht wieder als "faellig" erscheinen - dafuer wird
     * der zuletzt auf Flash gesicherte Stand geladen (siehe
     * kalender_anzeige_tablette_bestaetigen). */
    if (!neuer_tag) {
        for (int i = 0; i < neue_anzahl; i++) {
            if (!neue_eintraege[i].ist_tablette)
                continue;
            for (int a = 0; a < s_heute_anzahl; a++) {
                if (s_heute_eintraege[a].ist_tablette &&
                    strcmp(s_heute_eintraege[a].titel, neue_eintraege[i].titel) == 0) {
                    neue_eintraege[i].bestaetigt = s_heute_eintraege[a].bestaetigt;
                    break;
                }
            }
        }
    } else if (s_letzter_tag_schluessel == -1) {
        static char s_titel_gespeichert[KALENDER_EINTRAEGE_MAX][ICS_TITEL_MAX];
        int n_gespeichert = kalender_speicher_bestaetigungen_lesen(schluessel, s_titel_gespeichert,
                                                                    KALENDER_EINTRAEGE_MAX);
        for (int i = 0; i < neue_anzahl; i++) {
            if (!neue_eintraege[i].ist_tablette)
                continue;
            for (int b = 0; b < n_gespeichert; b++) {
                if (strcmp(s_titel_gespeichert[b], neue_eintraege[i].titel) == 0) {
                    neue_eintraege[i].bestaetigt = true;
                    break;
                }
            }
        }
        if (n_gespeichert > 0)
            ESP_LOGI(TAG, "%d bereits bestaetigte Tablette(n) von Flash uebernommen (nach Neustart)",
                     n_gespeichert);
    }
    memcpy(s_heute_eintraege, neue_eintraege, sizeof neue_eintraege[0] * (size_t)neue_anzahl);
    s_heute_anzahl = neue_anzahl;

    s_version++;
    xSemaphoreGive(s_mutex);

    s_letzter_tag_schluessel = schluessel;
    ESP_LOGI(TAG, "Anzeige aktualisiert (%d Termine/Tabletten heute)", n);
}

static void neuen_kalender_uebernehmen(char *puffer, size_t laenge)
{
    free(s_ics_text);
    s_ics_text = puffer;
    s_ics_laenge = laenge;
    kalender_speicher_schreiben(puffer, laenge);
}

static void task_funktion(void *arg)
{
    (void)arg;

    esp_err_t fehler = kalender_speicher_init();
    if (fehler != ESP_OK)
        ESP_LOGE(TAG, "Kalender-Speicher konnte nicht gemountet werden: %s",
                 esp_err_to_name(fehler));

    char *gecacht = NULL;
    size_t gecacht_laenge = 0;
    if (kalender_speicher_lesen(&gecacht, &gecacht_laenge) == ESP_OK && gecacht_laenge > 0) {
        ESP_LOGI(TAG, "Gecachten Kalender geladen (%u Bytes)", (unsigned)gecacht_laenge);
        s_ics_text = gecacht;
        s_ics_laenge = gecacht_laenge;
    }

    s_naechster_abruf_us = 0; /* sofort beim ersten Durchlauf versuchen */

    for (;;) {
        int64_t jetzt_us = esp_timer_get_time();

        if (netz_ist_verbunden() && jetzt_us >= s_naechster_abruf_us) {
            char *puffer = NULL;
            size_t laenge = 0;
            esp_err_t err = kalender_holen(&puffer, &laenge);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Kalender heruntergeladen (%u Bytes)", (unsigned)laenge);
                neuen_kalender_uebernehmen(puffer, laenge);
                s_frisch = true;
                einstellungen_letzter_kalender_sync_setzen(time(NULL));
                s_naechster_abruf_us = jetzt_us + ABRUF_INTERVALL_US;
                if (zeit_ist_synchron())
                    fuer_heute_neu_parsen();
            } else {
                s_naechster_abruf_us = jetzt_us + ABRUF_RETRY_US;
            }
        } else if (!netz_ist_verbunden()) {
            s_naechster_abruf_us = jetzt_us + KEIN_WLAN_RETRY_US;
        }

        /* Unabhaengig vom Netz: bei Tageswechsel (oder sobald die Uhrzeit
         * zum ersten Mal bekannt wird) aus dem vorhandenen Text neu
         * veroeffentlichen — kein Netz noetig, nur ein neues "heute". */
        if (zeit_ist_synchron() && s_ics_text) {
            struct tm lokal;
            if (heute_schluessel(&lokal) != s_letzter_tag_schluessel)
                fuer_heute_neu_parsen();
        }

        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}

void kalender_task_starten(void)
{
    s_mutex = xSemaphoreCreateMutex();
    /* 16K statt vormals 8K: fuer_heute_neu_parsen() haelt inzwischen
     * zusaetzlich zu den formatierten Texten (kalender_anzeige_t, ~1,3KB)
     * auch noch die strukturierten Tageseintraege (neue_eintraege, weitere
     * ~1,3KB) gleichzeitig auf dem Stack - zusammen mit dem 32er
     * ics_termin_t-Puffer (~3,8KB) reichte der alte Stack nicht mehr aus
     * (Stack-Overflow beschaedigte den Mutex-Handle, sichtbar als
     * "assert failed: xQueueSemaphoreTake ... uxItemSize == 0"). */
    xTaskCreate(task_funktion, "kalender", 16384, NULL, 4, NULL);
}

uint32_t kalender_anzeige_version(void)
{
    return s_version;
}

bool kalender_anzeige_frisch(void)
{
    return s_frisch;
}

int kalender_anzeige_heutige_eintraege(kalender_tag_eintrag_t *ziel, int max)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int anzahl = s_heute_anzahl < max ? s_heute_anzahl : max;
    memcpy(ziel, s_heute_eintraege, sizeof ziel[0] * (size_t)anzahl);
    xSemaphoreGive(s_mutex);
    return anzahl;
}

void kalender_anzeige_tablette_bestaetigen(int index, bool bestaetigt)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (index >= 0 && index < s_heute_anzahl) {
        s_heute_eintraege[index].bestaetigt = bestaetigt;

        /* Sofort auf Flash sichern, damit ein unerwarteter Neustart
         * (Stromausfall/Panic) eine bereits genommene Tablette nicht
         * wieder als "faellig" erscheinen laesst (siehe FALLSTRICKE #14 -
         * der Boot-Neustart-Reflex wurde deswegen schon abgeschaltet,
         * aber ein Reset kann trotzdem vorkommen). */
        static char s_titel_bestaetigt[KALENDER_EINTRAEGE_MAX][ICS_TITEL_MAX];
        int anzahl_bestaetigt = 0;
        for (int i = 0; i < s_heute_anzahl; i++) {
            if (s_heute_eintraege[i].ist_tablette && s_heute_eintraege[i].bestaetigt)
                snprintf(s_titel_bestaetigt[anzahl_bestaetigt++], ICS_TITEL_MAX, "%.*s",
                         ICS_TITEL_MAX - 1, s_heute_eintraege[i].titel);
        }
        kalender_speicher_bestaetigungen_schreiben(s_letzter_tag_schluessel, s_titel_bestaetigt,
                                                    anzahl_bestaetigt);
    }
    xSemaphoreGive(s_mutex);
}

void kalender_anzeige_jetzt_pruefen(void)
{
    s_naechster_abruf_us = 0;
}

kalender_tablette_status_t kalender_tablette_status(const kalender_tag_eintrag_t *eintrag,
                                                     bool zeit_bekannt, int jetzt_minuten)
{
    if (eintrag->bestaetigt)
        return KALENDER_TABLETTE_ABGEHAKT;
    if (!zeit_bekannt || eintrag->ganztags)
        return KALENDER_TABLETTE_ZUKUNFT;

    int soll_minuten = eintrag->stunde * 60 + eintrag->minute;
    if (jetzt_minuten < soll_minuten)
        return KALENDER_TABLETTE_ZUKUNFT;

    /* Einnahme-Zeitfenster: falls die Tablette eine echte DTEND-Uhrzeit hat
     * (main/ics_parser.h), gilt sie bis dahin als "faellig", statt der
     * sonst festen 60-Minuten-Schwelle - passend zu Terminen, die selbst
     * schon ein Zeitfenster vorgeben (z. B. "8:00-8:30 Uhr nuechtern"). */
    int grenze_minuten = eintrag->hat_ende
                              ? eintrag->end_stunde * 60 + eintrag->end_minute
                              : soll_minuten + KALENDER_TABLETTE_UEBERFAELLIG_MIN;
    if (jetzt_minuten >= grenze_minuten)
        return KALENDER_TABLETTE_UEBERFAELLIG;
    return KALENDER_TABLETTE_FAELLIG;
}

int kalender_anzeige_eintraege_fuer_tag(int tage_versatz, kalender_tag_eintrag_t *ziel, int max)
{
    time_t jetzt = time(NULL) + (time_t)tage_versatz * 86400;
    struct tm lokal;
    localtime_r(&jetzt, &lokal);

    /* Aus dem Stack in den PSRAM verlagert - siehe Kommentar bei
     * fuer_heute_neu_parsen() weiter oben. Diese Funktion laeuft im
     * schlimmsten Fall 7x verschachtelt (tagesansicht_tag_aktualisieren
     * faerbt beim Tageswechsel alle 7 Wochentag-Buttons neu), auf dem
     * main-/LVGL-Task war das mit einem termine[32]-Array auf dem Stack
     * live nicht mehr sicher (FALLSTRICKE #26). */
    ics_termin_t *termine = heap_caps_malloc(32 * sizeof(ics_termin_t), MALLOC_CAP_SPIRAM);
    if (!termine) {
        ESP_LOGE(TAG, "Kein PSRAM fuer Termin-Puffer");
        return 0;
    }
    int n = 0;

    /* Mutex bleibt waehrend des gesamten Parse-Vorgangs gehalten - der
     * Parser braucht nur wenige Millisekunden (reine Stringverarbeitung,
     * kein I/O), das ist kuerzer als s_ics_text sonst unter der Hand
     * ausgetauscht werden koennte (neuer Download in task_funktion). */
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_ics_text) {
        n = ics_termine_fuer_tag(s_ics_text, s_ics_laenge,
                                 lokal.tm_year + 1900, lokal.tm_mon + 1, lokal.tm_mday,
                                 termine, 32);
    }
    xSemaphoreGive(s_mutex);

    if (n <= 0) {
        heap_caps_free(termine);
        return 0;
    }

    int anzahl = n < max ? n : max;
    for (int i = 0; i < anzahl; i++)
        eintrag_uebernehmen(&ziel[i], &termine[i]);
    heap_caps_free(termine);
    return anzahl;
}
