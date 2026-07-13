/*
 * kalender_anzeige.h — verbindet Abruf, Cache und ICS-Parser zu den
 * Texten, die die UI anzeigen soll. Laeuft als eigene FreeRTOS-Task,
 * damit ein langsamer/haengender HTTPS-Abruf nie die LVGL-Anzeige blockiert.
 */
#ifndef KALENDER_ANZEIGE_H
#define KALENDER_ANZEIGE_H

#include <stdbool.h>
#include <stdint.h>

#define KALENDER_TEXT_MAX 640

typedef struct {
    char tabletten_text[KALENDER_TEXT_MAX]; /* mehrzeilig, "-" wenn keine */
    char termine_text[KALENDER_TEXT_MAX];
    bool hat_daten; /* false, solange noch nichts geparst werden konnte */
} kalender_anzeige_t;

void kalender_task_starten(void);

/* Aendert sich, sobald neue Daten veroeffentlicht wurden — die UI muss
 * nur bei Aenderung neu zeichnen, nicht bei jedem Sekunden-Tick. */
uint32_t kalender_anzeige_version(void);

void kalender_anzeige_kopieren(kalender_anzeige_t *ziel);

#endif
