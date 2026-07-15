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

/* Textfarbe der Buttons/Platzhalter passend zum Tag/Abend/Nacht-Modus -
 * wird vom Aufrufer bereits gesperrt aufgerufen (siehe modus_anwenden in
 * app_main.c), sperrt also selbst nicht erneut. */
void tagesansicht_farbe_setzen(lv_color_t farbe);

/* Sichtbarkeit der Buttons - nachts wie Tabletten/Termine komplett
 * ausgeblendet. Wird ebenfalls bereits gesperrt aufgerufen. */
void tagesansicht_sichtbarkeit_setzen(bool sichtbar);

#endif
