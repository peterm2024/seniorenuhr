#include "screenshot_speicher.h"

#include <time.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "zeit.h"

static const char *TAG = "screenshot_speicher";

#define PARTITION_NAME "screenshots"

/* Muss ein Vielfaches der 4-KB-Sektorgroesse sein (Erase-Einheit von
 * NOR-Flash), damit sich jeder Platz einzeln loeschen laesst, ohne
 * Nachbarn zu beruehren. 96 KB liegt komfortabel ueber der live gemessenen
 * RLE-Groesse (75-89 KB, siehe FAHRPLAN Nachtrag 25). Muss zu
 * tools/screenshot_gemeinsam.py passen. */
#define PLATZ_GROESSE (96u * 1024u)

#define MAGIC 0x31485353u /* "SSH1", little-endian im Speicher */

/* 32 Byte gesamt, siehe _Static_assert unten - muss zu
 * tools/screenshot_flash_abholen.py (KOPF_FORMAT) passen.
 *
 * Zwei Zeitangaben, fuer zwei verschiedene Zwecke (Peters Wunsch, Screenshots
 * mit dem seriellen Log korrelieren zu koennen):
 *   - boot_millis ist exakt dieselbe Groesse, die ESP-IDF auch dem "I (...)"-
 *     Praefix jeder Log-Zeile voranstellt (esp_log_timestamp() intern) - ein
 *     Screenshot laesst sich damit einer Log-Zeile DERSELBEN Boot-Sitzung
 *     direkt zuordnen, auch ohne dass die Uhr gestellt ist.
 *   - unix_zeit ist die tatsaechliche Uhrzeit (UTC, wie zeit.c sie fuehrt),
 *     bleibt aber ueber einen Neustart hinweg vergleichbar - anders als
 *     boot_millis, das bei jedem Neustart wieder bei 0 beginnt. 0, falls die
 *     Uhr beim Aufnehmen noch nie gestellt war (zeit_ist_synchron() == false,
 *     praktisch nur ganz am Boot-Anfang moeglich). zeit_manuell zeigt an, ob
 *     dieser Wert NTP-bestaetigt ist oder nur ein Naeherungswert (siehe
 *     zeit_ist_manuell_gesetzt() in zeit.h). */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t sequenz;
    uint32_t datengroesse;
    uint16_t breite;
    uint16_t hoehe;
    uint8_t komprimiert;
    uint32_t boot_millis;
    uint32_t unix_zeit;
    uint8_t zeit_manuell;
    uint8_t reserviert[6];
} platz_kopf_t;
_Static_assert(sizeof(platz_kopf_t) == 32, "Kopf muss exakt 32 Byte sein");

static const esp_partition_t *s_partition;
static uint32_t s_anzahl_plaetze;
static uint32_t s_naechster_platz;
static uint32_t s_naechste_sequenz = 1;
static bool s_initialisiert;

/* Liest beim allerersten Aufruf die Koepfe ALLER Plaetze, um Ringpuffer-
 * Position und Sequenzzaehler ueber einen Neustart hinweg fortzusetzen -
 * bewusst OHNE eigenen NVS-Eintrag: die Plaetze tragen ihren Zustand schon
 * selbst, ein zusaetzlicher Speicherort koennte nur aus dem Tritt geraten. */
static void initialisieren_falls_noetig(void)
{
    if (s_initialisiert)
        return;
    s_initialisiert = true;

    s_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                            ESP_PARTITION_SUBTYPE_ANY, PARTITION_NAME);
    if (!s_partition) {
        ESP_LOGW(TAG, "Partition '%s' nicht gefunden - Screenshots werden nicht abgelegt", PARTITION_NAME);
        return;
    }
    s_anzahl_plaetze = s_partition->size / PLATZ_GROESSE;

    uint32_t hoechste_sequenz = 0;
    uint32_t platz_der_hoechsten = 0;
    bool etwas_gefunden = false;

    for (uint32_t i = 0; i < s_anzahl_plaetze; i++) {
        platz_kopf_t kopf;
        if (esp_partition_read(s_partition, (size_t)i * PLATZ_GROESSE, &kopf, sizeof kopf) != ESP_OK)
            continue;
        if (kopf.magic != MAGIC)
            continue;
        etwas_gefunden = true;
        if (kopf.sequenz >= hoechste_sequenz) {
            hoechste_sequenz = kopf.sequenz;
            platz_der_hoechsten = i;
        }
    }

    if (etwas_gefunden) {
        s_naechster_platz = (platz_der_hoechsten + 1) % s_anzahl_plaetze;
        s_naechste_sequenz = hoechste_sequenz + 1;
        ESP_LOGI(TAG, "Vorhandene Screenshots gefunden (hoechste Sequenz %lu) - naechster Platz %lu/%lu",
                 (unsigned long)hoechste_sequenz, (unsigned long)s_naechster_platz, (unsigned long)s_anzahl_plaetze);
    } else {
        ESP_LOGI(TAG, "Screenshot-Partition leer (%lu Plaetze a %u KB)",
                 (unsigned long)s_anzahl_plaetze, (unsigned)(PLATZ_GROESSE / 1024));
    }
}

void screenshot_speicher_ablegen(const uint8_t *daten, uint32_t groesse,
                                  uint16_t breite, uint16_t hoehe, bool komprimiert)
{
    initialisieren_falls_noetig();
    if (!s_partition)
        return;

    if (sizeof(platz_kopf_t) + groesse > PLATZ_GROESSE) {
        ESP_LOGW(TAG, "Screenshot zu gross fuer einen Platz (%u + %lu > %u Byte) - nicht abgelegt",
                 (unsigned)sizeof(platz_kopf_t), (unsigned long)groesse, (unsigned)PLATZ_GROESSE);
        return;
    }

    uint32_t platz = s_naechster_platz;
    size_t offset = (size_t)platz * PLATZ_GROESSE;

    esp_err_t err = esp_partition_erase_range(s_partition, offset, PLATZ_GROESSE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Platz %lu konnte nicht geloescht werden: %s", (unsigned long)platz, esp_err_to_name(err));
        return;
    }

    platz_kopf_t kopf = {
        .magic = MAGIC,
        .sequenz = s_naechste_sequenz,
        .datengroesse = groesse,
        .breite = breite,
        .hoehe = hoehe,
        .komprimiert = komprimiert ? 1 : 0,
        .boot_millis = (uint32_t)(esp_timer_get_time() / 1000),
        .unix_zeit = zeit_ist_synchron() ? (uint32_t)time(NULL) : 0,
        .zeit_manuell = zeit_ist_manuell_gesetzt() ? 1 : 0,
    };

    err = esp_partition_write(s_partition, offset, &kopf, sizeof kopf);
    if (err == ESP_OK)
        err = esp_partition_write(s_partition, offset + sizeof kopf, daten, groesse);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Platz %lu konnte nicht beschrieben werden: %s", (unsigned long)platz, esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Screenshot in Flash abgelegt: Platz %lu/%lu, Sequenz %lu, %lu Byte, Boot-Zeit %lu ms",
             (unsigned long)platz, (unsigned long)s_anzahl_plaetze, (unsigned long)s_naechste_sequenz,
             (unsigned long)groesse, (unsigned long)kopf.boot_millis);

    s_naechster_platz = (s_naechster_platz + 1) % s_anzahl_plaetze;
    s_naechste_sequenz++;
}
