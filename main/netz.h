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
 * Waehlt per kurzem WLAN-Scan unter den bekannten (im NVS gespeicherten,
 * siehe netz_zugangsdaten_speichern) Netzen dasjenige aus, das gerade
 * sichtbar ist - praktisch, wenn das Geraet zwischen mehreren Orten
 * (z. B. Testaufbau zu Hause und Einsatzort) wechselt. Ohne gespeicherte
 * Netze werden die einkompilierten Zugangsdaten aus secrets.h verwendet.
 */
void netz_start(void);

/* true, sobald das Board aktuell eine IP-Adresse hat. */
bool netz_ist_verbunden(void);

/* Pausiert (true) bzw. entpausiert (false) den WLAN-Watchdog (30s ohne
 * Verbindung -> Neustart). Waehrend der Benutzer auf einem
 * Einrichtungsbildschirm Zugangsdaten eintippt, soll ein Verbindungsabbruch
 * im Hintergrund keinen ueberraschenden Neustart mitten in der Eingabe
 * ausloesen (siehe app_main.c). */
void netz_watchdog_pausieren(bool pausieren);

/* Fuegt ein WLAN-Netz zur Liste der bekannten Netze im NVS hinzu (oder
 * aktualisiert das Passwort, falls die SSID schon bekannt ist) - leicht
 * verschleiert abgelegt (kein kryptographisch starkes Verfahren, siehe
 * netz.c/verschleiern), und startet das Geraet neu, damit die Liste gleich
 * im normalen Boot-Ablauf verwendet wird. Bis zu 5 Netze werden gemerkt,
 * danach wird das aelteste verdraengt. Kehrt bei Erfolg nicht zurueck; nur
 * bei einem NVS-Fehler wird ein Fehlercode geliefert. */
esp_err_t netz_zugangsdaten_speichern(const char *ssid, const char *passwort);

#endif
