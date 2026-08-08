/*
 * kalender_speicher.h — lokaler Cache des Kalenders (ueberlebt Neustarts
 * und Internetausfaelle). FAT + Wear-Levelling auf einer eigenen
 * Flash-Partition ("speicher", siehe partitions.csv).
 */
#ifndef KALENDER_SPEICHER_H
#define KALENDER_SPEICHER_H

#include <stddef.h>
#include "esp_err.h"
#include "ics_parser.h"

esp_err_t kalender_speicher_init(void);

esp_err_t kalender_speicher_schreiben(const char *daten, size_t laenge);

/* *puffer wird per malloc belegt (Aufrufer muss free() aufrufen).
 * Kein Fehler, wenn noch nichts gecacht ist — dann *laenge == 0. */
esp_err_t kalender_speicher_lesen(char **puffer, size_t *laenge);

/* Persistiert Titel UND Bestaetigungs-Uhrzeit aller aktuell bestaetigten
 * Tabletten fuer den angegebenen Kalendertag (JJJJMMTT), damit ein bereits
 * genommenes Medikament einen unerwarteten Neustart (Stromausfall/Panic)
 * uebersteht, statt wieder als "faellig" zu erscheinen. Ueberschreibt die
 * vorherige Datei komplett (kein Anhaengen noetig - der Aufrufer uebergibt
 * jeweils den vollstaendigen aktuellen Stand).
 *
 * `minute[i]` ist die Uhrzeit der Bestaetigung in Minuten seit Mitternacht
 * (-1 = unbekannt) - sie unterscheidet spaeter puenktliche von verspaeteten
 * Einnahmen. Dateiformat je Zeile: "<minute>\t<titel>". */
esp_err_t kalender_speicher_bestaetigungen_schreiben(int tag_schluessel,
                                                      const char titel[][ICS_TITEL_MAX],
                                                      const int *minute,
                                                      int anzahl);

/* Liefert bis zu `max` gespeicherte Titel samt Bestaetigungs-Uhrzeit zurueck
 * (Rueckgabewert = Anzahl), aber NUR wenn der gespeicherte Tag-Schluessel
 * exakt zu `erwarteter_tag_schluessel` passt - Bestaetigungen vom Vortag
 * (oder aus einer noch leeren/fehlenden Datei) duerfen nicht auf einen
 * anderen Tag durchschlagen.
 *
 * Liest auch das aeltere Format ohne Uhrzeit (nur Titel je Zeile); dort
 * liefert `minute_ziel[i]` dann -1, damit ein laufendes Geraet beim Update
 * seine heutigen Bestaetigungen nicht verliert. `minute_ziel` darf NULL
 * sein, wenn die Uhrzeit nicht gebraucht wird. */
int kalender_speicher_bestaetigungen_lesen(int erwarteter_tag_schluessel,
                                            char titel_ziel[][ICS_TITEL_MAX],
                                            int *minute_ziel,
                                            int max);

#endif
