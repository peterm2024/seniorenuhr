/*
 * kalender_speicher.h — lokaler Cache des Kalenders (ueberlebt Neustarts
 * und Internetausfaelle). FAT + Wear-Levelling auf einer eigenen
 * Flash-Partition ("speicher", siehe partitions.csv).
 */
#ifndef KALENDER_SPEICHER_H
#define KALENDER_SPEICHER_H

#include <stddef.h>
#include "esp_err.h"

esp_err_t kalender_speicher_init(void);

esp_err_t kalender_speicher_schreiben(const char *daten, size_t laenge);

/* *puffer wird per malloc belegt (Aufrufer muss free() aufrufen).
 * Kein Fehler, wenn noch nichts gecacht ist — dann *laenge == 0. */
esp_err_t kalender_speicher_lesen(char **puffer, size_t *laenge);

#endif
