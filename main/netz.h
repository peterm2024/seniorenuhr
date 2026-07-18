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

/* Aktuelle WLAN-Signalstaerke in dBm (negativ, naeher an 0 = besser), oder
 * 0 wenn gerade nicht verbunden. Fuer eine Vor-Ort-Diagnose schwacher
 * Empfangsqualitaet (siehe Einstellungen-Menue in einrichtung.c). */
int netz_rssi_dbm(void);

/* Pausiert (true) bzw. entpausiert (false) den WLAN-Watchdog (30s ohne
 * Verbindung -> Neustart). Waehrend der Benutzer auf einem
 * Einrichtungsbildschirm Zugangsdaten eintippt, soll ein Verbindungsabbruch
 * im Hintergrund keinen ueberraschenden Neustart mitten in der Eingabe
 * ausloesen (siehe app_main.c). */
void netz_watchdog_pausieren(bool pausieren);

/* Lockert die Neustart-Schwelle des Watchdogs von 30s auf 1 Woche - einmal
 * von app_main() aufgerufen, sobald der Hauptbildschirm zum ersten Mal
 * erreicht ist (WLAN/Zeit/Kalender einmal erfolgreich durchgelaufen). Die
 * Anzeige von Uhrzeit/Tabletten/Terminen hat danach oberste Prioritaet -
 * lieber eine zeitweise veraltete Anzeige als eine nie endende
 * Boot-Neustart-Schleife bei schwachem WLAN (siehe FALLSTRICKE #14). */
void netz_watchdog_lockern(void);

/* Fuegt ein WLAN-Netz zur Liste der bekannten Netze im NVS hinzu (oder
 * aktualisiert das Passwort, falls die SSID schon bekannt ist) - leicht
 * verschleiert abgelegt (kein kryptographisch starkes Verfahren, siehe
 * netz.c/verschleiern), und startet das Geraet neu, damit die Liste gleich
 * im normalen Boot-Ablauf verwendet wird. Bis zu 5 Netze werden gemerkt,
 * danach wird das aelteste verdraengt. Kehrt bei Erfolg nicht zurueck; nur
 * bei einem NVS-Fehler wird ein Fehlercode geliefert. */
esp_err_t netz_zugangsdaten_speichern(const char *ssid, const char *passwort);

/* Bis zu so viele unterschiedliche Netze liefert ein Scan zurueck (siehe
 * netz_scan_ergebnisse) - reicht fuer jede realistische Nachbarschaft. */
#define NETZ_SCAN_MAX 16

typedef struct {
    char ssid[33];
    int8_t rssi;
} netz_scan_eintrag_t;

/* Stoesst einen WLAN-Scan im Hintergrund an (nicht blockierend - das Ergebnis
 * kommt per WIFI_EVENT_SCAN_DONE, siehe netz_scan_fertig/netz_scan_ergebnisse).
 * Fuer die "gefundene Netzwerke"-Dropdown-Liste im Einstellungen-Menue
 * (siehe einrichtung.c) - funktioniert auch waehrend das Geraet bereits mit
 * einem Netz verbunden ist (kurze Unterbrechung durch Kanalwechsel waehrend
 * des Scans ist normal und unkritisch). */
void netz_scan_starten(void);

/* true, sobald das Ergebnis des zuletzt gestarteten Scans bereit ist
 * (auch bei 0 gefundenen Netzen oder einem fehlgeschlagenen Scan-Start). */
bool netz_scan_fertig(void);

/* Liefert bis zu `max` gefundene Netze (nach Signalstaerke absteigend
 * sortiert, Duplikate durch Mesh/Repeater mit gleicher SSID zusammengefasst
 * auf den staerksten Wert, versteckte Netze ohne Namen ausgelassen).
 * Rueckgabe: Anzahl geschriebener Eintraege. Nur sinnvoll, nachdem
 * netz_scan_fertig() true zurueckgegeben hat. */
int netz_scan_ergebnisse(netz_scan_eintrag_t *ziel, int max);

#endif
