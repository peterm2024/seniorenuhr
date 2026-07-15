/*
 * kalender_holen.h — laedt den ICS-Kalender per HTTPS.
 */
#ifndef KALENDER_HOLEN_H
#define KALENDER_HOLEN_H

#include <stddef.h>
#include "esp_err.h"

/*
 * Laedt den Kalender von der effektiven Kalender-Adresse (siehe
 * einstellungen.h: manueller Override falls gesetzt, sonst KALENDER_ICS_URL
 * aus main/secrets.h).
 * *puffer wird per malloc (bevorzugt im PSRAM) belegt und muss vom
 * Aufrufer mit free() freigegeben werden. Bricht bei mehr als
 * einigen hundert KB ab (Notbremse gegen Speicherueberlauf).
 */
esp_err_t kalender_holen(char **puffer, size_t *laenge);

#endif
