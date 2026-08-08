#include "kalender_speicher.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"

static const char *TAG = "kalender_speicher";

#define BASISPFAD "/speicher"
#define DATEIPFAD BASISPFAD "/kalender.ics"
/* Dateiname im klassischen 8.3-Format (max. 8 Zeichen Basisname) - das
 * Projekt nutzt CONFIG_FATFS_LFN_NONE (keine langen Dateinamen, spart RAM),
 * ein laengerer Name wie "tabletten.txt" wird von FatFs als ungueltig
 * abgelehnt (fopen liefert dann NULL/errno=EINVAL). */
#define BESTAETIGUNGEN_PFAD BASISPFAD "/tablette.txt"

static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

esp_err_t kalender_speicher_init(void)
{
    const esp_vfs_fat_mount_config_t mount_cfg = {
        .max_files = 2,
        .format_if_mount_failed = true,
        .allocation_unit_size = 4096,
    };
    return esp_vfs_fat_spiflash_mount_rw_wl(BASISPFAD, "speicher", &mount_cfg, &s_wl_handle);
}

esp_err_t kalender_speicher_schreiben(const char *daten, size_t laenge)
{
    FILE *f = fopen(DATEIPFAD, "wb");
    if (!f) {
        ESP_LOGW(TAG, "Konnte Cache-Datei nicht zum Schreiben oeffnen");
        return ESP_FAIL;
    }
    size_t geschrieben = fwrite(daten, 1, laenge, f);
    fclose(f);
    return (geschrieben == laenge) ? ESP_OK : ESP_FAIL;
}

esp_err_t kalender_speicher_lesen(char **puffer, size_t *laenge)
{
    *puffer = NULL;
    *laenge = 0;

    FILE *f = fopen(DATEIPFAD, "rb");
    if (!f)
        return ESP_OK; /* noch nichts gecacht - kein Fehler */

    fseek(f, 0, SEEK_END);
    long groesse = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (groesse <= 0) {
        fclose(f);
        return ESP_OK;
    }

    char *daten = heap_caps_malloc((size_t)groesse, MALLOC_CAP_SPIRAM);
    if (!daten) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    size_t gelesen = fread(daten, 1, (size_t)groesse, f);
    fclose(f);
    if (gelesen != (size_t)groesse) {
        free(daten);
        return ESP_FAIL;
    }

    *puffer = daten;
    *laenge = gelesen;
    return ESP_OK;
}

esp_err_t kalender_speicher_bestaetigungen_schreiben(int tag_schluessel,
                                                      const char titel[][ICS_TITEL_MAX],
                                                      const int *minute,
                                                      int anzahl)
{
    FILE *f = fopen(BESTAETIGUNGEN_PFAD, "wb");
    if (!f) {
        ESP_LOGW(TAG, "Konnte Bestaetigungs-Datei nicht zum Schreiben oeffnen (errno=%d: %s)",
                 errno, strerror(errno));
        return ESP_FAIL;
    }
    fprintf(f, "%d\n", tag_schluessel);
    /* "<minute>\t<titel>" - der Tabulator kann in einem Kalendertitel nicht
     * vorkommen (der ICS-Parser liefert nur einzeilige, escape-aufgeloeste
     * Titel), taugt also als eindeutiger Trenner. */
    for (int i = 0; i < anzahl; i++)
        fprintf(f, "%d\t%s\n", minute ? minute[i] : -1, titel[i]);
    fclose(f);
    ESP_LOGI(TAG, "Bestaetigungen gespeichert: Tag=%d, %d Titel", tag_schluessel, anzahl);
    return ESP_OK;
}

int kalender_speicher_bestaetigungen_lesen(int erwarteter_tag_schluessel,
                                            char titel_ziel[][ICS_TITEL_MAX],
                                            int *minute_ziel,
                                            int max)
{
    FILE *f = fopen(BESTAETIGUNGEN_PFAD, "rb");
    if (!f) {
        ESP_LOGI(TAG, "Bestaetigungs-Datei existiert noch nicht");
        return 0; /* noch nichts gespeichert - kein Fehler */
    }

    /* Platz fuer Titel + "<minute>\t"-Praefix + Zeilenende - ohne den
     * Aufschlag schnitte ein langer Titel in der neuen Formatvariante ab. */
    char zeile[ICS_TITEL_MAX + 16];
    if (!fgets(zeile, sizeof zeile, f)) {
        ESP_LOGW(TAG, "Bestaetigungs-Datei ist leer");
        fclose(f);
        return 0;
    }
    int gespeicherter_tag = atoi(zeile);
    if (gespeicherter_tag != erwarteter_tag_schluessel) {
        ESP_LOGI(TAG, "Bestaetigungen verworfen: gespeicherter Tag=%d, erwartet=%d",
                 gespeicherter_tag, erwarteter_tag_schluessel);
        fclose(f); /* Bestaetigungen vom Vortag - nicht auf heute anwenden */
        return 0;
    }

    int anzahl = 0;
    while (anzahl < max && fgets(zeile, sizeof zeile, f)) {
        size_t laenge = strlen(zeile);
        if (laenge > 0 && zeile[laenge - 1] == '\n')
            zeile[laenge - 1] = '\0';
        if (zeile[0] == '\0')
            continue;

        /* Neues Format "<minute>\t<titel>"; fehlt der Tabulator, stammt die
         * Datei aus einer aelteren Firmware und enthaelt nur den Titel -
         * dann Uhrzeit unbekannt (-1), damit ein Geraet beim Update seine
         * heutigen Bestaetigungen behaelt statt sie zu verwerfen. */
        char *trenner = strchr(zeile, '\t');
        const char *titel_teil = zeile;
        int minute = -1;
        if (trenner) {
            *trenner = '\0';
            minute = atoi(zeile);
            titel_teil = trenner + 1;
        }
        if (titel_teil[0] == '\0')
            continue;

        snprintf(titel_ziel[anzahl], ICS_TITEL_MAX, "%s", titel_teil);
        if (minute_ziel)
            minute_ziel[anzahl] = minute;
        anzahl++;
    }
    fclose(f);
    ESP_LOGI(TAG, "Bestaetigungen gelesen: Tag=%d, %d Titel", gespeicherter_tag, anzahl);
    return anzahl;
}
