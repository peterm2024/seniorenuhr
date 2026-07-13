/*
 * startbildschirm.h — Boot-Anzeige: schwarzer Bildschirm mit drei
 * dunkelgrauen Symbolen (WLAN, Uhr, Kalender). Waehrend ein Schritt
 * laeuft, blinkt sein Symbol; ist er fertig, bleibt es weiss.
 */
#ifndef STARTBILDSCHIRM_H
#define STARTBILDSCHIRM_H

typedef enum {
    STARTBILDSCHIRM_WLAN = 0,
    STARTBILDSCHIRM_UHR = 1,
    STARTBILDSCHIRM_KALENDER = 2,
} startbildschirm_schritt_t;

/* Baut die drei Symbole auf dem aktuell aktiven LVGL-Screen auf. */
void startbildschirm_erstellen(void);

void startbildschirm_schritt_start(startbildschirm_schritt_t schritt);
void startbildschirm_schritt_fertig(startbildschirm_schritt_t schritt);

/* Entfernt den Startbildschirm wieder (nach dem Wechsel zur Hauptanzeige). */
void startbildschirm_aufraeumen(void);

#endif
