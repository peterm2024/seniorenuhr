#include "kalender_anzeige.h"
#include "einstellungen.h"
#include "kalender_holen.h"
#include "kalender_speicher.h"
#include "tabletten_protokoll.h"
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
/* Pause nach den ersten Fehlschlaegen - siehe Begruendung in task_funktion. */
#define ABRUF_KURZ_RETRY_US ((int64_t)5 * 1000000)
#define ABRUF_KURZE_VERSUCHE 3
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
/* true, sobald kalender_task_starten() den Task tatsaechlich erzeugt hat.
 * Gebraucht von ota.c: dessen Bewaehrungsprobe nach einem Update verlangt
 * einen geladenen Kalender - solange dieser Task gar nicht laeuft, kann die
 * Bedingung prinzipbedingt nicht erfuellt werden, und die Frist darf nicht
 * ablaufen (siehe FALLSTRICKE #41). */
static volatile bool s_task_laeuft = false;

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

/* Der "schwebende" Vortag: beim Tageswechsel hierher gerettet und erst nach
 * KALENDER_UEBERHANG_ENDE_STUNDE ins Langzeitprotokoll geschrieben. Bis dahin
 * werden die noch OFFENEN Tabletten daraus vorne in s_heute_eintraege
 * eingeblendet (mit vom_vortag = true), damit sie sichtbar und abhakbar
 * bleiben.
 *
 * Enthaelt bewusst AUCH die bereits bestaetigten: nur so ist der Tag beim
 * spaeteren Archivieren vollstaendig, und die Bilanz zaehlt nicht nur die
 * Versaeumnisse. */
static kalender_tag_eintrag_t s_vortag[KALENDER_EINTRAEGE_MAX];
static int s_vortag_anzahl = 0;
static int s_vortag_schluessel = -1; /* -1 = kein schwebender Vortag */

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
    ziel->bestaetigt_minute = -1;
    ziel->vom_vortag = false; /* frisch aus dem Kalender = immer der heutige Tag */
}

/* Schreibt den schwebenden Vortag ins Langzeitprotokoll und vergisst ihn.
 * `erzwingen` = true umgeht die Uhrzeitpruefung (gebraucht, wenn ein zweiter
 * Tageswechsel den bisherigen Vortag sonst still ueberschriebe).
 *
 * Aufrufer muss den Mutex halten: die Funktion liest s_vortag. Das Schreiben
 * selbst passiert auf einem lokalen Abzug, damit der Flash-Zugriff nicht die
 * Anzeige blockiert. */
static void vortag_archivieren_falls_faellig(bool erzwingen)
{
    if (s_vortag_schluessel < 0 || s_vortag_anzahl == 0)
        return;

    if (!erzwingen) {
        struct tm lokal;
        int heute = heute_schluessel(&lokal);
        /* Faellig, sobald die Ueberhang-Stunde erreicht ist - oder sobald der
         * Vortag gar nicht mehr "gestern" ist (Geraet lief ueber mehrere Tage
         * durch, ohne dass die Stunde je getroffen wurde). */
        bool stunde_erreicht = (heute != s_vortag_schluessel) &&
                               (lokal.tm_hour >= KALENDER_UEBERHANG_ENDE_STUNDE);
        if (!stunde_erreicht)
            return;
    }

    static tabletten_protokoll_eintrag_t s_archiv[KALENDER_EINTRAEGE_MAX];
    int anzahl = 0;
    for (int i = 0; i < s_vortag_anzahl && anzahl < KALENDER_EINTRAEGE_MAX; i++) {
        const kalender_tag_eintrag_t *alt = &s_vortag[i];
        s_archiv[anzahl].tag_schluessel = s_vortag_schluessel;
        /* Bewusst OHNE kalender_tablette_soll_minute(): im Protokoll steht die
         * Uhrzeit des jeweiligen Tages, nicht relativ zu heute. */
        s_archiv[anzahl].soll_minute = alt->stunde * 60 + alt->minute;
        s_archiv[anzahl].ende_minute = alt->hat_ende
                                           ? alt->end_stunde * 60 + alt->end_minute
                                           : s_archiv[anzahl].soll_minute + KALENDER_TABLETTE_UEBERFAELLIG_MIN;
        s_archiv[anzahl].ist_minute = alt->bestaetigt ? alt->bestaetigt_minute : -1;
        snprintf(s_archiv[anzahl].titel, ICS_TITEL_MAX, "%.*s", ICS_TITEL_MAX - 1, alt->titel);
        anzahl++;
    }

    int schluessel = s_vortag_schluessel;
    s_vortag_anzahl = 0;
    s_vortag_schluessel = -1;

    /* Ausserhalb des Mutex waere sauberer, ist hier aber nicht noetig: der
     * Vorgang laeuft hoechstens einmal taeglich und schreibt wenige hundert
     * Byte. */
    if (anzahl > 0) {
        tabletten_protokoll_tag_ablegen(s_archiv, anzahl);
        ESP_LOGI(TAG, "Vortag %d ins Protokoll uebernommen (%d Tablette(n))", schluessel, anzahl);
    }
}

/* Blendet die noch OFFENEN Tabletten des schwebenden Vortags vorne in
 * s_heute_eintraege ein - vorne, weil sie das Dringendste sind, was auf dem
 * Schirm steht. Aufrufer muss den Mutex halten.
 *
 * Wird bei JEDEM Parse-Lauf erneut aufgerufen (nicht nur beim Tageswechsel):
 * s_heute_eintraege wird alle 15 Minuten komplett neu aufgebaut, der Ueberhang
 * ginge sonst beim naechsten Kalender-Abruf verloren. */
static void ueberhang_einblenden(const struct tm *jetzt)
{
    if (s_vortag_schluessel < 0 || s_vortag_anzahl == 0)
        return;
    if (jetzt->tm_hour >= KALENDER_UEBERHANG_ENDE_STUNDE)
        return; /* Ueberhang abgelaufen - archiviert wird im naechsten Tick */

    /* Von hinten nach vorne einsetzen, damit die Reihenfolge des Vortags
     * erhalten bleibt. */
    for (int i = s_vortag_anzahl - 1; i >= 0; i--) {
        if (s_vortag[i].bestaetigt)
            continue; /* erledigt - gehoert nicht mehr auf den Schirm */
        if (s_heute_anzahl >= KALENDER_EINTRAEGE_MAX)
            break;
        memmove(&s_heute_eintraege[1], &s_heute_eintraege[0],
                sizeof s_heute_eintraege[0] * (size_t)s_heute_anzahl);
        s_heute_eintraege[0] = s_vortag[i];
        s_heute_eintraege[0].vom_vortag = true;
        s_heute_anzahl++;
    }
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
                    neue_eintraege[i].bestaetigt_minute = s_heute_eintraege[a].bestaetigt_minute;
                    break;
                }
            }
        }
    } else if (s_letzter_tag_schluessel == -1) {
        static char s_titel_gespeichert[KALENDER_EINTRAEGE_MAX][ICS_TITEL_MAX];
        static int s_minute_gespeichert[KALENDER_EINTRAEGE_MAX];
        int n_gespeichert = kalender_speicher_bestaetigungen_lesen(schluessel, s_titel_gespeichert,
                                                                    s_minute_gespeichert,
                                                                    KALENDER_EINTRAEGE_MAX);
        for (int i = 0; i < neue_anzahl; i++) {
            if (!neue_eintraege[i].ist_tablette)
                continue;
            for (int b = 0; b < n_gespeichert; b++) {
                if (strcmp(s_titel_gespeichert[b], neue_eintraege[i].titel) == 0) {
                    neue_eintraege[i].bestaetigt = true;
                    neue_eintraege[i].bestaetigt_minute = s_minute_gespeichert[b];
                    break;
                }
            }
        }
        if (n_gespeichert > 0)
            ESP_LOGI(TAG, "%d bereits bestaetigte Tablette(n) von Flash uebernommen (nach Neustart)",
                     n_gespeichert);
    }
    /* Letzte Gelegenheit, den abgeschlossenen Vortag zu retten: gleich
     * ueberschreibt das memcpy s_heute_eintraege unwiederbringlich. Er wird
     * hier NICHT sofort archiviert, sondern bleibt bis
     * KALENDER_UEBERHANG_ENDE_STUNDE in der Schwebe - bis dahin sind seine
     * offenen Tabletten noch abhakbar (siehe ueberhang_einblenden). Ein Urteil
     * "vergessen" um 00:00 waere verfrueht.
     *
     * Nur bei einem ECHTEN Tageswechsel im laufenden Betrieb: beim ersten
     * Parse nach dem Start (s_letzter_tag_schluessel == -1) liegt kein
     * vollstaendiger Vortag vor, und ein halber Tag wuerde die Bilanz
     * verfaelschen (siehe tabletten_protokoll.h). */
    if (neuer_tag && s_letzter_tag_schluessel != -1) {
        /* Ein noch nicht abgeschlossener aelterer Vortag (Geraet lief ueber
         * mehrere Tageswechsel, ohne dass 04:00 erreicht wurde - praktisch nur
         * bei verstellter Uhr) wird vorher weggeschrieben, statt still
         * ueberschrieben zu werden. */
        vortag_archivieren_falls_faellig(true);

        s_vortag_anzahl = 0;
        for (int i = 0; i < s_heute_anzahl && s_vortag_anzahl < KALENDER_EINTRAEGE_MAX; i++) {
            if (!s_heute_eintraege[i].ist_tablette || s_heute_eintraege[i].ganztags)
                continue; /* ohne Uhrzeit gibt es kein Einnahme-Fenster zu bewerten */
            if (s_heute_eintraege[i].vom_vortag)
                continue; /* Ueberhang des VORvortags nicht weiterreichen */
            s_vortag[s_vortag_anzahl++] = s_heute_eintraege[i];
        }
        s_vortag_schluessel = s_vortag_anzahl > 0 ? s_letzter_tag_schluessel : -1;
        if (s_vortag_anzahl > 0)
            ESP_LOGI(TAG, "Tageswechsel: %d Tablette(n) von %d bleiben bis %02d:00 nachhaengend",
                     s_vortag_anzahl, s_letzter_tag_schluessel, KALENDER_UEBERHANG_ENDE_STUNDE);
    }

    memcpy(s_heute_eintraege, neue_eintraege, sizeof neue_eintraege[0] * (size_t)neue_anzahl);
    s_heute_anzahl = neue_anzahl;
    ueberhang_einblenden(&lokal);

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

    int fehlversuche = 0; /* steuert die Pause bis zum naechsten Versuch */

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
                /* Erst jetzt ist bewiesen, dass die Adresse wirklich taugt -
                 * also festhalten, damit sie ein Firmware-Update ueberlebt.
                 * Dieselbe Ueberlegung wie beim WLAN (siehe netz.c): die
                 * Release-Firmware enthaelt statt der privaten Adresse nur
                 * den Platzhalter aus secrets.example.h, ein Geraet ohne
                 * gespeicherte Adresse stuende nach jedem Update ohne
                 * Kalender da. Der NVS wird von OTA nicht angefasst.
                 * Schreibt nur beim ersten Mal (siehe einstellungen.c). */
                char benutzte_url[EINSTELLUNGEN_KALENDER_URL_MAX];
                einstellungen_kalender_url_effektiv(benutzte_url, sizeof benutzte_url);
                einstellungen_kalender_url_sichern(benutzte_url);
                s_naechster_abruf_us = jetzt_us + ABRUF_INTERVALL_US;
                fehlversuche = 0;
                if (zeit_ist_synchron())
                    fuer_heute_neu_parsen();
            } else {
                /* Kurze Pause bei den ersten Fehlschlaegen, erst danach die
                 * volle halbe Minute. Grund (live gemessen, fix2.log): der
                 * ALLERERSTE Versuch faellt regelmaessig auf die Nase - der
                 * Task startet noch waehrend der Boot-Phasen und greift
                 * binnen Millisekunden zum Netz, waehrend der Startbildschirm
                 * den internen SRAM noch belegt (zweimal beobachtet:
                 * ESP_ERR_HTTP_CONNECT 23 ms bzw. wenige Sekunden nach dem
                 * Task-Start, der naechste Versuch nach dem Bildschirmwechsel
                 * gelang jedes Mal auf Anhieb). Mit 30 s Pause blieb das
                 * Kalender-Symbol danach eine halbe Minute lang
                 * durchgestrichen, obwohl das Netz laengst in Ordnung war -
                 * genau die "keine Sync"-Meldung, die Peter gesehen hat. */
                fehlversuche++;
                s_naechster_abruf_us = jetzt_us +
                    (fehlversuche <= ABRUF_KURZE_VERSUCHE ? ABRUF_KURZ_RETRY_US : ABRUF_RETRY_US);
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

        /* Laeuft der Ueberhang ab (siehe KALENDER_UEBERHANG_ENDE_STUNDE), den
         * Vortag endgueltig ins Protokoll schreiben und aus der Anzeige
         * nehmen. Hier im Tick statt im Parse-Lauf, weil zu dieser Stunde
         * weder ein Tageswechsel noch ein Kalender-Abruf faellig sein muss. */
        if (zeit_ist_synchron()) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            bool war_da = (s_vortag_schluessel >= 0);
            vortag_archivieren_falls_faellig(false);
            bool jetzt_weg = (s_vortag_schluessel < 0);
            xSemaphoreGive(s_mutex);
            /* Der Ueberhang stand bis eben in s_heute_eintraege - neu
             * aufbauen, damit er auch aus der Anzeige verschwindet. */
            if (war_da && jetzt_weg && s_ics_text)
                fuer_heute_neu_parsen();
        }

        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}

void kalender_task_starten(void)
{
    s_mutex = xSemaphoreCreateMutex();
    /* Frueher 8K, dann 16K: fuer_heute_neu_parsen() haelt zusaetzlich zu den
     * formatierten Texten (kalender_anzeige_t, ~1,3KB) auch die
     * strukturierten Tageseintraege (neue_eintraege, weitere ~1,3KB)
     * gleichzeitig auf dem Stack - zusammen mit dem 32er ics_termin_t-Puffer
     * (~3,8KB) reichten 8K nicht mehr (Stack-Overflow beschaedigte den
     * Mutex-Handle, sichtbar als "assert failed: xQueueSemaphoreTake ...
     * uxItemSize == 0").
     *
     * Jetzt 10K: der grosse ics_termin_t-Puffer liegt seit FALLSTRICKE #26
     * im PSRAM, die 16K stammen also noch aus der Zeit davor. Live gemessen
     * (uxTaskGetStackHighWaterMark nach Download und vollstaendigem Parsen):
     * 10060 Byte blieben ungenutzt, der echte Bedarf liegt bei gut 6,3 KB.
     * 10K laesst davon noch rund 3,7 KB Luft und gibt 6 KB internen SRAM
     * zurueck - genau die knappe Ressource, an der sonst das
     * Einstellungen-Menue scheiterte (siehe app_main.c).
     *
     * Stack statisch im .bss, nicht aus dem Heap: als der OTA-Task seinen
     * Stack dorthin bekam, schrumpfte der Heap so weit, dass hier kein
     * zusammenhaengender 10-KB-Block mehr frei war. Die Erzeugung schlug
     * fehl - und weil ihr Rueckgabewert nicht geprueft wurde, voellig
     * lautlos: der Kalender blieb einfach leer, die Boot-Phase lief in ihren
     * 60-Sekunden-Timeout. Statisch reserviert kann das nicht passieren. */
    static StackType_t stack[10240 / sizeof(StackType_t)];
    static StaticTask_t tcb;
    if (xTaskCreateStatic(task_funktion, "kalender", sizeof stack / sizeof stack[0],
                          NULL, 4, stack, &tcb) == NULL) {
        ESP_LOGE(TAG, "Kalender-Task konnte nicht gestartet werden - es gibt keine Termine");
        return;
    }
    s_task_laeuft = true;
}

bool kalender_task_laeuft(void)
{
    return s_task_laeuft;
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

void kalender_anzeige_tablette_bestaetigen(int index, bool bestaetigt, int jetzt_minuten)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (index >= 0 && index < s_heute_anzahl) {
        s_heute_eintraege[index].bestaetigt = bestaetigt;
        /* Uhrzeit nur beim Bestaetigen merken; beim Zuruecknehmen wieder
         * loeschen, damit eine spaetere erneute Bestaetigung nicht die alte
         * (womoeglich puenktliche) Zeit erbt. */
        s_heute_eintraege[index].bestaetigt_minute = bestaetigt ? jetzt_minuten : -1;

        /* Nachhaengender Eintrag von gestern: die Bestaetigung gehoert an den
         * schwebenden Vortag, sonst waere sie beim naechsten Kalender-Abruf
         * wieder verschwunden (s_heute_eintraege wird dabei neu aufgebaut) und
         * fehlte spaeter im Protokoll. Zugeordnet wird ueber den Titel -
         * dieselbe Kopplung wie beim Uebernehmen des Bestaetigt-Status. */
        if (s_heute_eintraege[index].vom_vortag) {
            for (int v = 0; v < s_vortag_anzahl; v++) {
                if (strcmp(s_vortag[v].titel, s_heute_eintraege[index].titel) != 0)
                    continue;
                s_vortag[v].bestaetigt = bestaetigt;
                /* Die Uhrzeit bleibt auf HEUTE bezogen (z.B. 70 = 01:10). Erst
                 * beim Archivieren wird daraus wieder eine Zeit des Vortags -
                 * dort steht dann eine Bestaetigung "nach Mitternacht"
                 * korrekterweise als spaet, aber erfolgt da. */
                s_vortag[v].bestaetigt_minute = bestaetigt ? jetzt_minuten + 24 * 60 : -1;
                break;
            }
        }

        /* Sofort auf Flash sichern, damit ein unerwarteter Neustart
         * (Stromausfall/Panic) eine bereits genommene Tablette nicht
         * wieder als "faellig" erscheinen laesst (siehe FALLSTRICKE #14 -
         * der Boot-Neustart-Reflex wurde deswegen schon abgeschaltet,
         * aber ein Reset kann trotzdem vorkommen). */
        static char s_titel_bestaetigt[KALENDER_EINTRAEGE_MAX][ICS_TITEL_MAX];
        static int s_minute_bestaetigt[KALENDER_EINTRAEGE_MAX];
        int anzahl_bestaetigt = 0;
        for (int i = 0; i < s_heute_anzahl; i++) {
            /* Nachhaengende Eintraege gehoeren NICHT in die Tagesdatei: die
             * gilt fuer s_letzter_tag_schluessel (heute), sie stammen aber von
             * gestern. Sonst erschiene die gestrige Tablette nach einem
             * Neustart als heute bereits genommen. Ihr Ueberleben sichert
             * stattdessen der schwebende Vortag weiter oben. */
            if (s_heute_eintraege[i].vom_vortag)
                continue;
            if (s_heute_eintraege[i].ist_tablette && s_heute_eintraege[i].bestaetigt) {
                snprintf(s_titel_bestaetigt[anzahl_bestaetigt], ICS_TITEL_MAX, "%.*s",
                         ICS_TITEL_MAX - 1, s_heute_eintraege[i].titel);
                s_minute_bestaetigt[anzahl_bestaetigt] = s_heute_eintraege[i].bestaetigt_minute;
                anzahl_bestaetigt++;
            }
        }
        kalender_speicher_bestaetigungen_schreiben(s_letzter_tag_schluessel, s_titel_bestaetigt,
                                                    s_minute_bestaetigt, anzahl_bestaetigt);
    }
    xSemaphoreGive(s_mutex);
}

int kalender_tablette_soll_minute(const kalender_tag_eintrag_t *eintrag)
{
    int soll_minuten = eintrag->stunde * 60 + eintrag->minute;
    /* Ein nachhaengender Eintrag von gestern liegt vor dem heutigen
     * Mitternacht - dadurch ergeben Vergleiche gegen "Minuten seit heute
     * 00:00" ohne Sonderfaelle das Richtige. */
    return eintrag->vom_vortag ? soll_minuten - 24 * 60 : soll_minuten;
}

int kalender_tablette_fenster_ende(const kalender_tag_eintrag_t *eintrag)
{
    int soll_minuten = kalender_tablette_soll_minute(eintrag);
    if (!eintrag->hat_ende)
        return soll_minuten + KALENDER_TABLETTE_UEBERFAELLIG_MIN;

    int ende = eintrag->end_stunde * 60 + eintrag->end_minute;
    if (eintrag->vom_vortag)
        ende -= 24 * 60;
    return ende;
}

bool kalender_tablette_puenktlich_bestaetigt(const kalender_tag_eintrag_t *eintrag)
{
    if (!eintrag->bestaetigt)
        return false;
    /* Ganztaegige Tabletten haben kein sinnvolles Zeitfenster, und ohne
     * bekannte Bestaetigungszeit (alte Speicherdatei, oder Uhr war beim
     * Abhaken nicht synchron) wird zugunsten des Nutzers "puenktlich"
     * angenommen - lieber kein Vorwurf als ein falscher. */
    if (eintrag->ganztags || eintrag->bestaetigt_minute < 0)
        return true;
    return eintrag->bestaetigt_minute < kalender_tablette_fenster_ende(eintrag);
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

    int soll_minuten = kalender_tablette_soll_minute(eintrag);
    if (jetzt_minuten < soll_minuten)
        return KALENDER_TABLETTE_ZUKUNFT;

    /* Einnahme-Zeitfenster: falls die Tablette eine echte DTEND-Uhrzeit hat
     * (main/ics_parser.h), gilt sie bis dahin als "faellig", statt der
     * sonst festen 60-Minuten-Schwelle - passend zu Terminen, die selbst
     * schon ein Zeitfenster vorgeben (z. B. "8:00-8:30 Uhr nuechtern"). */
    if (jetzt_minuten >= kalender_tablette_fenster_ende(eintrag))
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
