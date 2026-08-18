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
    char beschreibung[ICS_BESCHREIBUNG_MAX]; /* leer, wenn nicht gesetzt */
    int stunde;
    int minute;
    int end_stunde;
    int end_minute;
    bool hat_ende;    /* true nur bei echter DTEND-Uhrzeit, siehe ics_parser.h */
    bool ganztags;
    bool ist_tablette;
    bool bestaetigt;
    /* Uhrzeit der Bestaetigung als Minuten seit Mitternacht, -1 wenn
     * unbestaetigt (oder aus einer alten Speicherdatei ohne Zeitangabe
     * uebernommen). Wird mitgespeichert, damit "im erlaubten Fenster
     * bestaetigt" (gruen) und "zu spaet bestaetigt" (bernstein) auch nach
     * einem Neustart noch unterscheidbar sind - ohne die Uhrzeit wuerde eine
     * puenktlich genommene Tablette spaeter faelschlich als verspaetet
     * gelten, weil nur noch "jetzt" bekannt waere. */
    int bestaetigt_minute;
    /* true = dieser Eintrag stammt vom VORTAG und haengt nur noch nach
     * (siehe KALENDER_UEBERHANG_ENDE_STUNDE). Alle Zeitrechnungen muessen das
     * beruecksichtigen: eine 23:00-Tablette von gestern ist um 01:00 nicht
     * "in 22 Stunden faellig", sondern laengst ueberfaellig. Deshalb wird ihre
     * Soll-Zeit um einen ganzen Tag zurueckversetzt (siehe
     * kalender_tablette_soll_minute). */
    bool vom_vortag;
} kalender_tag_eintrag_t;

void kalender_task_starten(void);

/* true, sobald kalender_task_starten() den Task erzeugt hat. Der Aufruf
 * steht in app_main.c ERST NACH den Boot-Phasen WLAN und Uhr - bleibt der
 * Ablauf dort haengen (z. B. weil der Benutzer im Einstellungen-Menue des
 * Startbildschirms steht), laeuft dieser Task nie an und
 * kalender_anzeige_version() bleibt zwangslaeufig 0. Genau das muss die
 * Bewaehrungsprobe in ota.c unterscheiden koennen: "Kalender noch nicht
 * geladen" ist etwas anderes als "Kalender kann gar nicht laden"
 * (siehe FALLSTRICKE #41). */
bool kalender_task_laeuft(void);

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
 * kalender_anzeige_heutige_eintraege()). `jetzt_minuten` ist die aktuelle
 * Uhrzeit in Minuten seit Mitternacht (-1, falls unbekannt) und wird beim
 * Bestaetigen mitgespeichert, damit spaeter erkennbar bleibt, ob es
 * innerhalb des erlaubten Einnahme-Fensters geschah. */
void kalender_anzeige_tablette_bestaetigen(int index, bool bestaetigt, int jetzt_minuten);

/* Ende des erlaubten Einnahme-Fensters in Minuten seit Mitternacht: die
 * DTEND-Uhrzeit, falls im Kalender gesetzt, sonst
 * KALENDER_TABLETTE_UEBERFAELLIG_MIN nach der Soll-Zeit. Oeffentlich, weil
 * die Anzeige damit entscheidet, ob eine Bestaetigung noch als puenktlich
 * gilt - dieselbe Schwelle wie in kalender_tablette_status(). */
int kalender_tablette_fenster_ende(const kalender_tag_eintrag_t *eintrag);

/* true, wenn die Tablette bestaetigt ist UND das nachweislich innerhalb des
 * erlaubten Fensters geschah. Bei Bestaetigungen ohne bekannte Uhrzeit
 * (alte Speicherdatei) wird zugunsten des Nutzers "puenktlich" angenommen. */
bool kalender_tablette_puenktlich_bestaetigt(const kalender_tag_eintrag_t *eintrag);

/* Faelligkeitsstatus einer Tablette - gemeinsam fuer alle Anzeigeorte
 * (Hauptuebersicht in app_main.c, "Heute"-Fenster in tagesansicht.c), damit
 * die Schwelle nur an einer Stelle gepflegt wird. Jeder Ort bildet die
 * Werte auf seine eigene, dort schon vorhandene Farbpalette ab. */
typedef enum {
    KALENDER_TABLETTE_ABGEHAKT,     /* bereits bestaetigt - gedaempft darstellen */
    KALENDER_TABLETTE_ZUKUNFT,      /* noch nicht faellig - normale Farbe */
    KALENDER_TABLETTE_FAELLIG,      /* Einnahmezeit erreicht, noch nicht bestaetigt */
    KALENDER_TABLETTE_UEBERFAELLIG, /* Einnahme-Zeitfenster abgelaufen, immer
                                      * noch unbestaetigt - Ende des Fensters
                                      * ist die DTEND-Uhrzeit, falls im ICS
                                      * gesetzt (kalender_tag_eintrag_t.hat_ende),
                                      * sonst KALENDER_TABLETTE_UEBERFAELLIG_MIN
                                      * nach der Soll-Zeit */
} kalender_tablette_status_t;

#define KALENDER_TABLETTE_UEBERFAELLIG_MIN 60

/* Bis zu dieser Stunde haengen noch nicht abgehakte Tabletten des Vortags
 * nach: sie bleiben sichtbar und abhakbar, und der Vortag wandert erst danach
 * ins Langzeitprotokoll (Peters Entscheidung 18.08.2026).
 *
 * Der Anlass: eine 23:00-Tablette verschwand um Mitternacht spurlos und war
 * nicht mehr abzuhaken - wer sie um 23:55 nahm und erst um 00:10 bestaetigen
 * wollte, stand dauerhaft als Versaeumnis in der Bilanz.
 *
 * 04:00 als Grenze ist eine Abwaegung: "nachts nochmal aufgestanden" soll
 * gehen, aber wer um 7 Uhr aufsteht, bekommt eine 23-Uhr-Tablette bewusst
 * NICHT mehr angeboten - acht Stunden zu spaet nachnehmen waere bei den
 * meisten Medikamenten falsch, und das Geraet soll dazu nicht auffordern. */
#define KALENDER_UEBERHANG_ENDE_STUNDE 4

/* Soll-Zeit in Minuten, bezogen auf HEUTE 00:00 - fuer Eintraege vom Vortag
 * also negativ. Einzige Stelle, an der diese Umrechnung stattfindet; alles
 * andere (Status, Fenster-Ende, Anzeige) baut darauf auf. */
int kalender_tablette_soll_minute(const kalender_tag_eintrag_t *eintrag);

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
