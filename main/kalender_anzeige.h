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

/* Faelligkeitsstatus einer Tablette - gemeinsam fuer alle Anzeigeorte
 * (Hauptuebersicht in app_main.c, "Heute"-Fenster in tagesansicht.c), damit
 * die Schwelle nur an einer Stelle gepflegt wird. Jeder Ort bildet die
 * Werte auf seine eigene, dort schon vorhandene Farbpalette ab. */
typedef enum {
    KALENDER_TABLETTE_ABGEHAKT,     /* bereits bestaetigt - gedaempft darstellen */
    KALENDER_TABLETTE_ZUKUNFT,      /* noch nicht faellig - normale Farbe */
    KALENDER_TABLETTE_FAELLIG,      /* Einnahmezeit erreicht, noch nicht bestaetigt */
    KALENDER_TABLETTE_UEBERFAELLIG, /* seit KALENDER_TABLETTE_UEBERFAELLIG_MIN
                                      * unbestaetigt ueberfaellig */
} kalender_tablette_status_t;

#define KALENDER_TABLETTE_UEBERFAELLIG_MIN 60

/* `zeit_bekannt`/`jetzt_minuten` wie an anderen Stellen: jetzt_minuten nur
 * gueltig, wenn zeit_bekannt true ist (zeit_ist_synchron()). Ohne bekannte
 * Uhrzeit oder bei einem ganztaegigen Eintrag bleibt der Status immer
 * KALENDER_TABLETTE_ZUKUNFT (keine verlaessliche Zeitgrundlage). */
kalender_tablette_status_t kalender_tablette_status(const kalender_tag_eintrag_t *eintrag,
                                                     bool zeit_bekannt, int jetzt_minuten);

/* Rein lesende Eintraege fuer einen beliebigen anderen Tag (Versatz in
 * Tagen zu heute, z. B. -1 = gestern, +1 = morgen) - fuer die Tages-Fenster
 * der uebrigen Wochentag-Buttons. Kein Bestaetigungsstatus (immer false).
 * Rueckgabe: Anzahl geschriebener Eintraege. */
int kalender_anzeige_eintraege_fuer_tag(int tage_versatz, kalender_tag_eintrag_t *ziel, int max);

/* Erzwingt einen sofortigen Abrufversuch (statt auf den naechsten planmaessigen
 * Zeitpunkt zu warten) - fuer den "gleich einen Resync probieren"-Knopf im
 * Status-Detail-Fenster (app_main.c). Wirkt nur, wenn WLAN verbunden ist;
 * greift spaetestens beim naechsten Tick der Kalender-Task (TICK_MS). */
void kalender_anzeige_jetzt_pruefen(void);

#endif
