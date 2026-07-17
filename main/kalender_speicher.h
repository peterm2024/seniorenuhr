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

/* Persistiert die Titel aller aktuell bestaetigten Tabletten fuer den
 * angegebenen Kalendertag (JJJJMMTT), damit ein bereits genommenes
 * Medikament einen unerwarteten Neustart (Stromausfall/Panic) uebersteht,
 * statt wieder als "faellig" zu erscheinen. Ueberschreibt die vorherige
 * Datei komplett (kein Anhaengen noetig - der Aufrufer uebergibt jeweils
 * den vollstaendigen aktuellen Stand). */
esp_err_t kalender_speicher_bestaetigungen_schreiben(int tag_schluessel,
                                                      const char titel[][ICS_TITEL_MAX],
                                                      int anzahl);

/* Liefert bis zu `max` gespeicherte Titel zurueck (Rueckgabewert = Anzahl),
 * aber NUR wenn der gespeicherte Tag-Schluessel exakt zu
 * `erwarteter_tag_schluessel` passt - Bestaetigungen vom Vortag (oder aus
 * einer noch leeren/fehlenden Datei) duerfen nicht auf einen anderen Tag
 * durchschlagen. */
int kalender_speicher_bestaetigungen_lesen(int erwarteter_tag_schluessel,
                                            char titel_ziel[][ICS_TITEL_MAX],
                                            int max);

#endif
