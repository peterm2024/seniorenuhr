/*
 * kalender_anzeige.h — verbindet Abruf, Cache und ICS-Parser zu den
 * Texten, die die UI anzeigen soll. Laeuft als eigene FreeRTOS-Task,
 * damit ein langsamer/haengender HTTPS-Abruf nie die LVGL-Anzeige blockiert.
 */
#ifndef KALENDER_ANZEIGE_H
#define KALENDER_ANZEIGE_H

#include <stdbool.h>
#include <stdint.h>

#include "ics_parser.h"

#define KALENDER_TEXT_MAX 640

/* Fuer die Tagesansicht (Wochentag-Buttons + Tages-/Heute-Fenster, siehe
 * tagesansicht.c): ein einzelner Termin/Tabletten-Eintrag mit optionalem
 * Bestaetigungsstatus (nur bei kalender_anzeige_heutige_eintraege()
 * sinnvoll gesetzt, sonst immer false). */
#define KALENDER_EINTRAEGE_MAX 12

typedef struct {
    char titel[ICS_TITEL_MAX];
    int stunde;
    int minute;
    bool ganztags;
    bool ist_tablette;
    bool bestaetigt;
} kalender_tag_eintrag_t;

void kalender_task_starten(void);

/* Aendert sich, sobald neue Daten veroeffentlicht wurden — die UI muss
 * nur bei Aenderung neu zeichnen, nicht bei jedem Sekunden-Tick. */
uint32_t kalender_anzeige_version(void);

/* true, sobald in dieser Sitzung mindestens ein echter Netz-Download
 * gelungen ist (im Unterschied zu kalender_anzeige_version() != 0, das
 * auch bei rein gecachten, nie frisch heruntergeladenen Daten zutrifft -
 * z. B. im Offline-Betrieb ohne WLAN). */
bool kalender_anzeige_frisch(void);

/* Strukturierte Eintraege fuer HEUTE inkl. Bestaetigungsstatus (fuer das
 * Tabletten-Abhaken im "Heute"-Fenster). Der Status bleibt bis Mitternacht
 * bestehen, auch ueber zwischenzeitliche Kalender-Refreshes hinweg (per
 * Titel-Abgleich uebernommen). Rueckgabe: Anzahl geschriebener Eintraege. */
int kalender_anzeige_heutige_eintraege(kalender_tag_eintrag_t *ziel, int max);

/* Bestaetigt/entbestaetigt die Tablette an Index `index` (Index aus
 * kalender_anzeige_heutige_eintraege()). */
void kalender_anzeige_tablette_bestaetigen(int index, bool bestaetigt);

/* Rein lesende Eintraege fuer einen beliebigen anderen Tag (Versatz in
 * Tagen zu heute, z. B. -1 = gestern, +1 = morgen) - fuer die Tages-Fenster
 * der uebrigen Wochentag-Buttons. Kein Bestaetigungsstatus (immer false).
 * Rueckgabe: Anzahl geschriebener Eintraege. */
int kalender_anzeige_eintraege_fuer_tag(int tage_versatz, kalender_tag_eintrag_t *ziel, int max);

#endif
