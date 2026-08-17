#include "zeit.h"
#include "texte.h"
#include "einstellungen.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"

static const char *TAG = "zeit";

static volatile bool s_zeit_synchron = false;
static volatile bool s_zeit_manuell = false; /* true: zuletzt manuell gesetzt, nicht NTP-bestaetigt */

static void sync_callback(struct timeval *tv)
{
    (void)tv;
    if (!s_zeit_synchron)
        ESP_LOGI(TAG, "Uhrzeit per NTP synchronisiert");
    s_zeit_synchron = true;
    s_zeit_manuell = false; /* NTP hat Vorrang - ab jetzt wieder bestaetigt */
    einstellungen_letzte_sync_setzen(time(NULL));
}

void zeit_zeitzone_setzen(void)
{
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}

void zeit_sntp_starten(void)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.sync_cb = sync_callback;
    ESP_ERROR_CHECK(esp_netif_sntp_init(&cfg));
}

bool zeit_ist_synchron(void)
{
    return s_zeit_synchron;
}

bool zeit_ist_manuell_gesetzt(void)
{
    return s_zeit_manuell;
}

void zeit_manuell_setzen(int tag, int monat, int jahr, int stunde, int minute)
{
    struct tm t = {0};
    t.tm_mday = tag;
    t.tm_mon = monat - 1;
    t.tm_year = jahr - 1900;
    t.tm_hour = stunde;
    t.tm_min = minute;
    t.tm_isdst = -1; /* Sommerzeit anhand der TZ-Regel automatisch ermitteln */

    struct timeval tv = { .tv_sec = mktime(&t), .tv_usec = 0 };
    settimeofday(&tv, NULL);

    ESP_LOGI(TAG, "Uhrzeit manuell gesetzt (kein NTP): %04d-%02d-%02d %02d:%02d",
             jahr, monat, tag, stunde, minute);
    s_zeit_synchron = true;
    s_zeit_manuell = true;
}

void zeit_uebernehmen(time_t zeitstempel)
{
    struct timeval tv = { .tv_sec = zeitstempel, .tv_usec = 0 };
    settimeofday(&tv, NULL);

    ESP_LOGW(TAG, "Uhrzeit vom letzten bekannten Stand uebernommen (kein NTP, keine manuelle Eingabe)");
    s_zeit_synchron = true;
    s_zeit_manuell = true;
}

const char *zeit_wochentag_gross(const struct tm *t)
{
    static const char *const namen[SPRACHE_ANZAHL][7] = {
        { "SONNTAG", "MONTAG", "DIENSTAG", "MITTWOCH", "DONNERSTAG", "FREITAG", "SAMSTAG" },
        { "SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY" },
    };
    if (t->tm_wday < 0 || t->tm_wday > 6)
        return "";
    return namen[sprache_aktuell()][t->tm_wday];
}

const char *zeit_wochentag_kurz(const struct tm *t)
{
    /* Zwei Zeichen, weil die Wochentag-Spalte des Hauptbildschirms genau
     * dafuer bemessen ist - im Englischen waeren drei ueblicher ("Sun"),
     * das passt aber nicht in die Buttons. */
    static const char *const namen[SPRACHE_ANZAHL][7] = {
        { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa" },
        { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" },
    };
    if (t->tm_wday < 0 || t->tm_wday > 6)
        return "";
    return namen[sprache_aktuell()][t->tm_wday];
}

void zeit_datum_text(const struct tm *t, char *puffer, size_t puffer_groesse)
{
    static const char *const monate[SPRACHE_ANZAHL][12] = {
        { "Januar", "Februar", "März", "April", "Mai", "Juni",
          "Juli", "August", "September", "Oktober", "November", "Dezember" },
        { "January", "February", "March", "April", "May", "June",
          "July", "August", "September", "October", "November", "December" },
    };
    sprache_t sprache = sprache_aktuell();
    const char *monat = (t->tm_mon >= 0 && t->tm_mon < 12) ? monate[sprache][t->tm_mon] : "";
    /* Nicht nur die Monatsnamen, auch die REIHENFOLGE unterscheidet sich:
     * "10. August 2026" gegenueber "August 10, 2026". */
    if (sprache == SPRACHE_ENGLISCH)
        snprintf(puffer, puffer_groesse, "%s %d, %d", monat, t->tm_mday, t->tm_year + 1900);
    else
        snprintf(puffer, puffer_groesse, "%d. %s %d", t->tm_mday, monat, t->tm_year + 1900);
}

const char *zeit_tageszeit(const struct tm *t)
{
    int stunde = t->tm_hour;
    if (stunde >= 22 || stunde < 6)
        return text(TXT_NACHT);
    if (stunde < 12)
        return text(TXT_VORMITTAG);
    if (stunde < 18)
        return text(TXT_NACHMITTAG);
    return text(TXT_ABEND);
}
