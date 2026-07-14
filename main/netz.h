/*
 * netz.h — WLAN-Verbindung (Station-Modus) mit automatischem Reconnect.
 */
#ifndef NETZ_H
#define NETZ_H

#include <stdbool.h>
#include "esp_err.h"

/*
 * Initialisiert WLAN und stoesst die Verbindung an. Kehrt sofort zurueck,
 * ohne auf die erste IP-Adresse zu warten — der Aufrufer prueft dafuer
 * netz_ist_verbunden() (siehe app_main: einheitliches Warten mit
 * Timeout/Countdown-Anzeige fuer alle Boot-Phasen). Im Hintergrund wird
 * die Verbindung bei Abbruch automatisch neu aufgebaut.
 *
 * Verwendet WLAN-Zugangsdaten aus dem NVS, falls dort per
 * netz_zugangsdaten_speichern() welche hinterlegt wurden, sonst die
 * einkompilierten aus secrets.h.
 */
void netz_start(void);

/* true, sobald das Board aktuell eine IP-Adresse hat. */
bool netz_ist_verbunden(void);

/* Speichert neue WLAN-Zugangsdaten dauerhaft im NVS (haben ab dem naechsten
 * netz_start() Vorrang vor secrets.h) und startet das Geraet neu, damit die
 * neuen Daten gleich im normalen Boot-Ablauf ausprobiert werden. Kehrt bei
 * Erfolg nicht zurueck; nur bei einem NVS-Fehler wird ein Fehlercode
 * geliefert. */
esp_err_t netz_zugangsdaten_speichern(const char *ssid, const char *passwort);

#endif
