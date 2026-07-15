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

/* Nach 30s ohne Fortschritt bietet der Startbildschirm zwei Buttons an,
 * die den Countdown beenden und dem Benutzer eine manuelle Einstellung
 * ermoeglichen (siehe einrichtung.h). Das Zahnrad-Symbol unten rechts ist
 * dagegen von Anfang an sichtbar und fuehrt zum Einstellungen-Menue, das
 * dieselben zwei Optionen zusaetzlich zu Rotation/Buzzer/Kalender-Adresse
 * enthaelt. */
typedef enum {
    STARTBILDSCHIRM_AKTION_KEINE = 0,
    STARTBILDSCHIRM_AKTION_WLAN_WECHSELN,
    STARTBILDSCHIRM_AKTION_OFFLINE,
    STARTBILDSCHIRM_AKTION_EINSTELLUNGEN,
} startbildschirm_aktion_t;

/* Baut die drei Symbole auf dem aktuell aktiven LVGL-Screen auf. */
void startbildschirm_erstellen(void);

void startbildschirm_schritt_start(startbildschirm_schritt_t schritt);
void startbildschirm_schritt_fertig(startbildschirm_schritt_t schritt);

/* Liefert die zuletzt angetippte Aktion und setzt sie danach auf KEINE
 * zurueck (einmal abholen = konsumiert). */
startbildschirm_aktion_t startbildschirm_aktion_abfragen(void);

/* Aktiviert den Startbildschirm erneut als sichtbaren Screen - fuer die
 * Rueckkehr aus einem Einrichtungsbildschirm (WLAN/Zeit), der zwischendurch
 * einen eigenen Screen geladen hatte. */
void startbildschirm_reaktivieren(void);

/* Entfernt den Startbildschirm wieder (nach dem Wechsel zur Hauptanzeige). */
void startbildschirm_aufraeumen(void);

#endif
