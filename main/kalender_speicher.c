#include "kalender_speicher.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"

static const char *TAG = "kalender_speicher";

#define BASISPFAD "/speicher"
#define DATEIPFAD BASISPFAD "/kalender.ics"
#define BESTAETIGUNGEN_PFAD BASISPFAD "/tabletten.txt"

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
                                                      int anzahl)
{
    FILE *f = fopen(BESTAETIGUNGEN_PFAD, "wb");
    if (!f) {
        ESP_LOGW(TAG, "Konnte Bestaetigungs-Datei nicht zum Schreiben oeffnen");
        return ESP_FAIL;
    }
    fprintf(f, "%d\n", tag_schluessel);
    for (int i = 0; i < anzahl; i++)
        fprintf(f, "%s\n", titel[i]);
    fclose(f);
    return ESP_OK;
}

int kalender_speicher_bestaetigungen_lesen(int erwarteter_tag_schluessel,
                                            char titel_ziel[][ICS_TITEL_MAX],
                                            int max)
{
    FILE *f = fopen(BESTAETIGUNGEN_PFAD, "rb");
    if (!f)
        return 0; /* noch nichts gespeichert - kein Fehler */

    char zeile[ICS_TITEL_MAX + 8];
    if (!fgets(zeile, sizeof zeile, f)) {
        fclose(f);
        return 0;
    }
    if (atoi(zeile) != erwarteter_tag_schluessel) {
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
        snprintf(titel_ziel[anzahl], ICS_TITEL_MAX, "%s", zeile);
        anzahl++;
    }
    fclose(f);
    return anzahl;
}
