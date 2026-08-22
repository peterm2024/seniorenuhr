#include "tabletten_protokoll.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "tabletten_protokoll";

/* Dateiname im 8.3-Format - das Projekt baut mit CONFIG_FATFS_LFN_NONE, ein
 * laengerer Basisname wird von FatFs abgelehnt (siehe kalender_speicher.c).
 * Ueberschreibbar, damit der Host-Test (test_host/test_protokoll.c) in ein
 * temporaeres Verzeichnis schreiben kann - die Auswertelogik laesst sich sonst
 * nicht pruefen, ohne auf dem Geraet echte Mitternachtswechsel abzuwarten. */
#ifndef PROTOKOLL_PFAD
#define PROTOKOLL_PFAD "/speicher/tabprot.txt"
#endif
/* Beim Umschreiben (Kuerzen) genutzt, danach ueber rename an seinen Platz -
 * so kann ein Stromausfall mittendrin die bestehende Datei nicht zerstoeren. */
#ifndef PROTOKOLL_TEMP_PFAD
#define PROTOKOLL_TEMP_PFAD "/speicher/tabprot.tmp"
#endif

/* Die "speicher"-Partition hat insgesamt nur 256K und beherbergt auch den
 * Kalender-Cache. Eine Zeile ist gut 40 Byte, ein Tag mit 5 Tabletten also
 * ~200 Byte - 40K reichen damit fuer etwa ein halbes Jahr. Wird die Grenze
 * ueberschritten, fallen die AELTESTEN Tage weg (Ringpuffer-Prinzip wie bei
 * den Screenshots): ein Rueckblick ist fuer die juengste Vergangenheit
 * wertvoll, fuer den vorletzten Winter nicht. */
#define PROTOKOLL_MAX_BYTES (40 * 1024)
/* Beim Kuerzen gleich ordentlich Luft schaffen, statt bei jedem weiteren Tag
 * erneut die ganze Datei umzuschreiben (Flash-Schonung). */
#define PROTOKOLL_ZIEL_BYTES (30 * 1024)

/* Ab dieser Verspaetung gilt eine Einnahme als "deutlich zu spaet" und wird in
 * der Bilanz namentlich aufgefuehrt. Wer 5 Minuten nach Fenster-Ende bestaetigt,
 * soll nicht in einer Maengelliste auftauchen - das wuerde die Liste mit
 * Belanglosem fuellen und die echten Ausreisser verdecken. */
#define AUFFAELLIG_AB_MIN 30

/* Eine Zeile: <tag>\t<soll>\t<ende>\t<ist>\t<titel>\n
 * Titel zuletzt, damit ein Tabulator darin (theoretisch moeglich) den Rest
 * nicht verschiebt - beim Lesen wird der Titel als "alles bis Zeilenende"
 * genommen. */
#define ZEILE_MAX (32 + ICS_TITEL_MAX)

/* Zerlegt eine Zeile. Rueckgabe false bei unlesbaren/leeren Zeilen; die
 * werden ueberall stillschweigend uebersprungen, damit eine einzelne
 * beschaedigte Zeile (abgebrochener Schreibvorgang) nicht die gesamte
 * Auswertung unbrauchbar macht. */
static bool zeile_zerlegen(const char *zeile, tabletten_protokoll_eintrag_t *ziel)
{
    int tag = 0, soll = 0, ende = 0, ist = 0, titel_pos = 0;
    if (sscanf(zeile, "%d\t%d\t%d\t%d\t%n", &tag, &soll, &ende, &ist, &titel_pos) < 4)
        return false;
    if (titel_pos <= 0 || tag < 19700101)
        return false;

    ziel->tag_schluessel = tag;
    ziel->soll_minute = soll;
    ziel->ende_minute = ende;
    ziel->ist_minute = ist;
    snprintf(ziel->titel, sizeof ziel->titel, "%s", zeile + titel_pos);

    /* Zeilenende abschneiden - fgets liefert es mit. */
    size_t len = strlen(ziel->titel);
    while (len > 0 && (ziel->titel[len - 1] == '\n' || ziel->titel[len - 1] == '\r'))
        ziel->titel[--len] = '\0';
    return len > 0;
}

/* Schreibt die Datei ohne ihren aeltesten Teil neu, sobald sie zu gross wird.
 * Schneidet dabei an einer TAGESGRENZE, nicht mitten in einem Tag - ein halb
 * erhaltener Tag wuerde die Bilanz verfaelschen ("2 von 2 genommen", obwohl
 * an dem Tag 5 faellig waren). */
static void bei_bedarf_kuerzen(void)
{
    FILE *f = fopen(PROTOKOLL_PFAD, "rb");
    if (!f)
        return;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return;
    }
    long groesse = ftell(f);
    if (groesse < PROTOKOLL_MAX_BYTES) {
        fclose(f);
        return;
    }

    /* Ab hier wird umgeschrieben: alles ab dem ersten Tageswechsel hinter der
     * Ueberhang-Marke uebernehmen. */
    long ueberspringen = groesse - PROTOKOLL_ZIEL_BYTES;
    rewind(f);

    FILE *neu = fopen(PROTOKOLL_TEMP_PFAD, "wb");
    if (!neu) {
        ESP_LOGW(TAG, "Kuerzen nicht moeglich - Temp-Datei nicht anlegbar (errno=%d)", errno);
        fclose(f);
        return;
    }

    /* Uebernommen wird ab der ERSTEN ZEILE DES ERSTEN TAGES, der komplett
     * hinter der Marke beginnt. Es genuegt ausdruecklich nicht, ab der Marke
     * zu uebernehmen: die liegt fast immer mitten in einem Tag, dessen erste
     * Tabletten dann fehlten - die Bilanz meldete fuer ihn z.B. "2 von 2
     * genommen", obwohl 6 faellig waren. */
    char zeile[ZEILE_MAX];
    long position = 0;      /* Byte-Offset des ANFANGS der aktuellen Zeile */
    int vorheriger_tag = -1;
    bool uebernehmen = false;
    int behaltene_zeilen = 0;
    while (fgets(zeile, sizeof zeile, f)) {
        size_t laenge = strlen(zeile);
        tabletten_protokoll_eintrag_t e;
        if (!zeile_zerlegen(zeile, &e)) {
            position += (long)laenge;
            continue;
        }
        if (!uebernehmen && e.tag_schluessel != vorheriger_tag && position >= ueberspringen)
            uebernehmen = true;
        vorheriger_tag = e.tag_schluessel;
        position += (long)laenge;

        if (uebernehmen) {
            fputs(zeile, neu);
            behaltene_zeilen++;
        }
    }
    fclose(f);
    fclose(neu);

    /* Kein einziger Tageswechsel hinter der Marke - dann lieber die zu grosse
     * Datei behalten als das gesamte Protokoll zu verlieren. Praktisch kaum
     * erreichbar (ein Tag fasst hoechstens KALENDER_EINTRAEGE_MAX Zeilen),
     * aber der Datenverlust waere endgueltig. */
    if (behaltene_zeilen == 0) {
        ESP_LOGW(TAG, "Kuerzen uebersprungen - keine Tagesgrenze hinter der Marke gefunden");
        remove(PROTOKOLL_TEMP_PFAD);
        return;
    }

    remove(PROTOKOLL_PFAD);
    if (rename(PROTOKOLL_TEMP_PFAD, PROTOKOLL_PFAD) != 0) {
        ESP_LOGE(TAG, "Kuerzen fehlgeschlagen - Protokoll konnte nicht ersetzt werden (errno=%d)", errno);
        return;
    }
    ESP_LOGI(TAG, "Protokoll gekuerzt: %ld -> ~%d Byte (%d Zeilen behalten)",
             groesse, PROTOKOLL_ZIEL_BYTES, behaltene_zeilen);
}

bool tabletten_protokoll_kennt_tag(int tag_schluessel)
{
    FILE *f = fopen(PROTOKOLL_PFAD, "rb");
    if (!f)
        return false;

    char zeile[ZEILE_MAX];
    bool gefunden = false;
    while (!gefunden && fgets(zeile, sizeof zeile, f)) {
        tabletten_protokoll_eintrag_t e;
        if (zeile_zerlegen(zeile, &e) && e.tag_schluessel == tag_schluessel)
            gefunden = true;
    }
    fclose(f);
    return gefunden;
}

esp_err_t tabletten_protokoll_tag_ablegen(const tabletten_protokoll_eintrag_t *eintraege, int anzahl)
{
    if (!eintraege || anzahl <= 0)
        return ESP_ERR_INVALID_ARG;

    /* Idempotenz: derselbe Tag darf nicht zweimal in der Bilanz landen. Der
     * Fall ist real - startet das Geraet kurz nach Mitternacht neu, wird der
     * Tageswechsel erneut erkannt. */
    if (tabletten_protokoll_kennt_tag(eintraege[0].tag_schluessel)) {
        ESP_LOGI(TAG, "Tag %d ist bereits aufgezeichnet - nicht erneut abgelegt",
                 eintraege[0].tag_schluessel);
        return ESP_OK;
    }

    FILE *f = fopen(PROTOKOLL_PFAD, "ab");
    if (!f) {
        ESP_LOGW(TAG, "Protokoll nicht zum Anhaengen zu oeffnen (errno=%d: %s)", errno, strerror(errno));
        return ESP_FAIL;
    }
    for (int i = 0; i < anzahl; i++) {
        fprintf(f, "%d\t%d\t%d\t%d\t%s\n",
                eintraege[i].tag_schluessel, eintraege[i].soll_minute,
                eintraege[i].ende_minute, eintraege[i].ist_minute, eintraege[i].titel);
    }
    fclose(f);

    ESP_LOGI(TAG, "Tag %d aufgezeichnet (%d Tablette(n))", eintraege[0].tag_schluessel, anzahl);
    bei_bedarf_kuerzen();
    return ESP_OK;
}

int tabletten_protokoll_tag_lesen(int tag_schluessel, tabletten_protokoll_eintrag_t *ziel, int max)
{
    if (!ziel || max <= 0)
        return 0;

    FILE *f = fopen(PROTOKOLL_PFAD, "rb");
    if (!f)
        return 0;

    char zeile[ZEILE_MAX];
    int anzahl = 0;
    while (anzahl < max && fgets(zeile, sizeof zeile, f)) {
        tabletten_protokoll_eintrag_t e;
        if (zeile_zerlegen(zeile, &e) && e.tag_schluessel == tag_schluessel)
            ziel[anzahl++] = e;
    }
    fclose(f);
    return anzahl;
}

tabletten_zustand_t tabletten_protokoll_zustand(const tabletten_protokoll_eintrag_t *eintrag)
{
    if (!eintrag || eintrag->ist_minute < 0)
        return TABLETTEN_ZUSTAND_VERGESSEN;
    /* ">=" und nicht ">": genau auf der Fenstergrenze bestaetigt heisst, das
     * erlaubte Fenster war bereits vorbei. Diese Grenze stammt aus der
     * urspruenglichen Bilanz und bleibt bewusst unveraendert - eine
     * Verschiebung um eine Minute wuerde alte Aufzeichnungen neu bewerten. */
    if (eintrag->ende_minute >= 0 && eintrag->ist_minute >= eintrag->ende_minute)
        return TABLETTEN_ZUSTAND_ZU_SPAET;
    return TABLETTEN_ZUSTAND_GENOMMEN;
}

esp_err_t tabletten_protokoll_bilanz_ermitteln(int ab_tag_schluessel, tabletten_protokoll_bilanz_t *ziel)
{
    if (!ziel)
        return ESP_ERR_INVALID_ARG;
    memset(ziel, 0, sizeof *ziel);

    FILE *f = fopen(PROTOKOLL_PFAD, "rb");
    if (!f)
        return ESP_OK; /* noch nichts aufgezeichnet - genullte Bilanz ist die ehrliche Antwort */

    char zeile[ZEILE_MAX];
    int letzter_tag = -1;
    while (fgets(zeile, sizeof zeile, f)) {
        tabletten_protokoll_eintrag_t e;
        if (!zeile_zerlegen(zeile, &e))
            continue;
        if (e.tag_schluessel < ab_tag_schluessel)
            continue;

        if (e.tag_schluessel != letzter_tag) {
            ziel->tage++;
            letzter_tag = e.tag_schluessel;
        }
        ziel->gesamt++;

        bool auffaellig = false;
        switch (tabletten_protokoll_zustand(&e)) {
        case TABLETTEN_ZUSTAND_VERGESSEN:
            ziel->vergessen++;
            auffaellig = true;
            break;
        case TABLETTEN_ZUSTAND_ZU_SPAET:
            ziel->zu_spaet++;
            /* Nur deutliche Verspaetungen sind eine Auffaelligkeit - fuenf
             * Minuten spaeter ist kein Befund, sondern Alltag. */
            auffaellig = (e.ist_minute - e.ende_minute) >= AUFFAELLIG_AB_MIN;
            break;
        case TABLETTEN_ZUSTAND_GENOMMEN:
            ziel->genommen++;
            break;
        }

        if (auffaellig) {
            ziel->auffaellig_gesamt++;
            /* Die JUENGSTEN Auffaelligkeiten behalten: ist die Liste voll,
             * rutscht der aelteste Eintrag heraus. Die Datei ist chronologisch,
             * das Neueste kommt also zuletzt - und interessiert am meisten. */
            if (ziel->auffaellig_anzahl < TABLETTEN_PROTOKOLL_AUFFAELLIG_MAX) {
                ziel->auffaellig[ziel->auffaellig_anzahl++] = e;
            } else {
                memmove(&ziel->auffaellig[0], &ziel->auffaellig[1],
                        sizeof ziel->auffaellig[0] * (TABLETTEN_PROTOKOLL_AUFFAELLIG_MAX - 1));
                ziel->auffaellig[TABLETTEN_PROTOKOLL_AUFFAELLIG_MAX - 1] = e;
            }
        }
    }
    fclose(f);
    return ESP_OK;
}
