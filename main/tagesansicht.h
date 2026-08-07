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

/* Oeffnet das Erinnerungsfenster fuer EINE faellige Tablette (Index aus
 * kalender_anzeige_heutige_eintraege) - poppt zur Einnahmezeit von selbst
 * auf, statt nur passiv die Zeile einzufaerben. Bewusst fokussiert auf
 * genau diese eine Tablette: grosse Schrift, eine einzige Handlung.
 * Bestaetigt wird mit demselben Schieber wie im "Heute"-Fenster (nicht mit
 * einem Button) - eine zufaellige Beruehrung darf eine Tablette niemals
 * faelschlich als genommen markieren. Schliesst sich nach kurzer Zeit von
 * selbst wieder (ERINNERUNG_ANZEIGEDAUER_MS), damit es Uhrzeit und Datum
 * nicht dauerhaft verdeckt; das Wiederholen uebernimmt app_main.c. */
void tagesansicht_erinnerung_zeigen(int index);

/* true, solange ein Tages- oder Heute-Fenster als Overlay ueber dem
 * Hauptbildschirm offen ist - waehrend dieser Zeit soll die Anzeige nicht
 * in den Abend-/Nacht-Modus zurueckfallen, auch wenn die 30s-Beruehrungs-
 * Wachzeit (app_main.c) laengst abgelaufen ist: Presses auf Elemente
 * *innerhalb* des Fensters (Schieberegler, Schliessen-Button, ...) bubbeln
 * nicht zum Hauptbildschirm durch und wuerden die Wachzeit sonst nicht
 * verlaengern. */
bool tagesansicht_fenster_offen(void);

/* Ruhiges Hinweisfenster waehrend eines laufenden OTA-Downloads (siehe
 * ota.h) - Fortschrittsbalken statt einer stillen Aktion im Hintergrund,
 * damit die Eltern den anschliessenden Neustart einordnen koennen. Von
 * app_main.c/uhr_tick anhand von ota_laeuft() gesteuert. Das "X" schliesst
 * nur die Anzeige, nicht den Download selbst. */
void tagesansicht_update_fenster_zeigen(void);
/* prozent < 0: Groesse des Downloads unbekannt, zeigt nur "Laedt...". */
void tagesansicht_update_fenster_fortschritt_setzen(int prozent);
void tagesansicht_update_fenster_schliessen(void);

#endif
