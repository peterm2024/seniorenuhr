/*
 * tagesansicht.h — Wochentag-Navigation auf der Hauptanzeige: 7 kleine
 * Buttons links (gestern .. +5 Tage, "heute" nur als Pfeil-Platzhalter)
 * oeffnen ein 15s lang eingeblendetes Tages-Fenster mit den Terminen/
 * Tabletten des jeweiligen Tages. Ein hochkanter Button rechts oeffnet
 * das "Heute"-Fenster mit Schiebeschaltern zum Abhaken der Tabletten.
 */
#ifndef TAGESANSICHT_H
#define TAGESANSICHT_H

#include "lvgl.h"

/* Baut die Buttons auf `scr` auf. Muss VOR dem Dimm-Overlay des
 * aufrufenden Screens erzeugt werden, damit Abend-Dimmung/Nacht-Ausblenden
 * (siehe tagesansicht_farbe_setzen/-sichtbarkeit_setzen) mit greifen. */
void tagesansicht_erstellen(lv_obj_t *scr);

/* Aktualisiert die Wochentag-Beschriftung der Buttons - taeglich einmal
 * beim Tageswechsel noetig, aber gefahrlos jede Sekunde aufrufbar (intern
 * ueber Aenderungserkennung gegen unnoetige Redraws abgesichert). */
void tagesansicht_tag_aktualisieren(void);

/* Sichtbarkeit der Buttons - nachts wie Tabletten/Termine komplett
 * ausgeblendet. Wird ebenfalls bereits gesperrt aufgerufen. */
void tagesansicht_sichtbarkeit_setzen(bool sichtbar);

/* Oeffnet das "Heute"-Fenster programmatisch, genau wie ein Tipp auf den
 * "Heute"-Button - fuer die antippbare Tabletten/Termine-Uebersicht auf dem
 * Hauptbildschirm (siehe app_main.c). */
void tagesansicht_heute_oeffnen(void);

/* true, solange ein Tages- oder Heute-Fenster als Overlay ueber dem
 * Hauptbildschirm offen ist - waehrend dieser Zeit soll die Anzeige nicht
 * in den Abend-/Nacht-Modus zurueckfallen, auch wenn die 30s-Beruehrungs-
 * Wachzeit (app_main.c) laengst abgelaufen ist: Presses auf Elemente
 * *innerhalb* des Fensters (Schieberegler, Schliessen-Button, ...) bubbeln
 * nicht zum Hauptbildschirm durch und wuerden die Wachzeit sonst nicht
 * verlaengern. */
bool tagesansicht_fenster_offen(void);

#endif
