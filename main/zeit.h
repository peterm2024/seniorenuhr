/*
 * zeit.h — Zeitzone (Europe/Berlin) und NTP-Synchronisation.
 */
#ifndef ZEIT_H
#define ZEIT_H

#include <stdbool.h>
#include <time.h>

/* Setzt die Zeitzone Europe/Berlin (inkl. automatischer Sommerzeit).
 * Vor jeder Verwendung von localtime() aufrufen; funktioniert auch
 * ohne bestehende WLAN-Verbindung. */
void zeit_zeitzone_setzen(void);

/* Startet den NTP-Client im Hintergrund (nicht blockierend).
 * Erst sinnvoll aufrufbar, nachdem netz_start() das Netzwerk
 * initialisiert hat. */
void zeit_sntp_starten(void);

/* true, sobald die Uhrzeit einmal erfolgreich per NTP gesetzt wurde. */
bool zeit_ist_synchron(void);

/* Deutscher Wochentag in Grossbuchstaben, z. B. "MONTAG". */
const char *zeit_wochentag_gross(const struct tm *t);

/* Schreibt das Datum als "10. Juli 2026" nach puffer. */
void zeit_datum_text(const struct tm *t, char *puffer, size_t puffer_groesse);

/* Tageszeit in einem Wort: "Vormittag", "Nachmittag", "Abend" oder "Nacht". */
const char *zeit_tageszeit(const struct tm *t);

#endif
