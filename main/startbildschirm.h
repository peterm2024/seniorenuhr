/*
 * startbildschirm.h — Boot-Anzeige: schwarzer Bildschirm mit drei
 * dunkelgrauen Symbolen (WLAN, Uhr, Kalender). Waehrend ein Schritt
 * laeuft, blinkt sein Symbol; ein Countdown-Ring darum zeigt die
 * verbleibende Zeit. Ist der Schritt fertig, bleiben Symbol und
 * (wieder aufgefuellter) Ring weiss stehen.
 */
#ifndef STARTBILDSCHIRM_H
#define STARTBILDSCHIRM_H

/* So lange darf jede Boot-Phase dauern; der Ring um das aktive Symbol
 * leert sich in dieser Zeit. Laeuft eine Phase in den Timeout, startet
 * app_main das Board neu. */
#define STARTBILDSCHIRM_PHASE_TIMEOUT_S 60

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
