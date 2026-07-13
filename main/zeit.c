#include "zeit.h"

#include <stdio.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"

static const char *TAG = "zeit";

static volatile bool s_zeit_synchron = false;

static void sync_callback(struct timeval *tv)
{
    (void)tv;
    if (!s_zeit_synchron)
        ESP_LOGI(TAG, "Uhrzeit per NTP synchronisiert");
    s_zeit_synchron = true;
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

const char *zeit_wochentag_gross(const struct tm *t)
{
    static const char *namen[7] = {
        "SONNTAG", "MONTAG", "DIENSTAG", "MITTWOCH",
        "DONNERSTAG", "FREITAG", "SAMSTAG"
    };
    if (t->tm_wday < 0 || t->tm_wday > 6)
        return "";
    return namen[t->tm_wday];
}

void zeit_datum_text(const struct tm *t, char *puffer, size_t puffer_groesse)
{
    static const char *monate[12] = {
        "Januar", "Februar", "März", "April", "Mai", "Juni",
        "Juli", "August", "September", "Oktober", "November", "Dezember"
    };
    const char *monat = (t->tm_mon >= 0 && t->tm_mon < 12) ? monate[t->tm_mon] : "";
    snprintf(puffer, puffer_groesse, "%d. %s %d", t->tm_mday, monat, t->tm_year + 1900);
}

const char *zeit_tageszeit(const struct tm *t)
{
    int stunde = t->tm_hour;
    if (stunde >= 22 || stunde < 6)
        return "Nacht";
    if (stunde < 12)
        return "Vormittag";
    if (stunde < 18)
        return "Nachmittag";
    return "Abend";
}
