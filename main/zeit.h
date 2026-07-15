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

/* true, sobald die Uhrzeit einmal gesetzt wurde - per NTP oder manuell
 * (siehe zeit_manuell_setzen). */
bool zeit_ist_synchron(void);

/* Setzt die Systemzeit direkt, ohne NTP (Offline-Betrieb ueber den
 * Einrichtungsbildschirm). tag/monat/jahr/stunde/minute in ueblicher
 * Schreibweise (monat 1-12, jahr z.B. 2026). Sommerzeit wird anhand der
 * TZ-Regel automatisch beruecksichtigt. */
void zeit_manuell_setzen(int tag, int monat, int jahr, int stunde, int minute);

/* true, wenn die aktuell gueltige Zeit zuletzt manuell gesetzt wurde statt
 * per NTP bestaetigt - wird automatisch wieder false, sobald ein echter
 * NTP-Sync gelingt (siehe zeit_sntp_starten). */
bool zeit_ist_manuell_gesetzt(void);

/* Deutscher Wochentag in Grossbuchstaben, z. B. "MONTAG". */
const char *zeit_wochentag_gross(const struct tm *t);

/* Deutscher Wochentag abgekuerzt auf 2 Buchstaben, z. B. "Mo". */
const char *zeit_wochentag_kurz(const struct tm *t);

/* Schreibt das Datum als "10. Juli 2026" nach puffer. */
void zeit_datum_text(const struct tm *t, char *puffer, size_t puffer_groesse);

/* Tageszeit in einem Wort: "Vormittag", "Nachmittag", "Abend" oder "Nacht". */
const char *zeit_tageszeit(const struct tm *t);

#endif
