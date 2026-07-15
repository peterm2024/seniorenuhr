/*
 * einstellungen.h — Persistente Benutzereinstellungen (NVS), erreichbar
 * ueber den neuen Settings-Bildschirm (siehe einrichtung.h). Unabhaengig
 * von netz.c's WLAN-Profilen: eigener NVS-Namensraum, kein Geheimnis-Schutz
 * noetig (keine Zugangsdaten).
 */
#ifndef EINSTELLUNGEN_H
#define EINSTELLUNGEN_H

#include <stdbool.h>
#include <time.h>

#define EINSTELLUNGEN_KALENDER_URL_MAX 256

/* Liest alle Werte einmalig aus dem NVS in einen RAM-Cache. Muss vor dem
 * ersten Zugriff auf irgendeine andere Funktion dieses Moduls aufgerufen
 * werden - ganz am Anfang von app_main(). Initialisiert bei Bedarf selbst
 * das NVS (nvs_flash_init ist idempotent, ein zweiter Aufruf z. B. aus
 * netz_start() ist ein harmloses No-Op). */
void einstellungen_laden(void);

/* Signalton bei Erinnerungen - reiner Vorbereitungs-Schalter, so lange noch
 * kein Buzzer verbaut ist (siehe FAHRPLAN.md "Spaeter/Ideen"). */
bool einstellungen_buzzer_aktiv(void);
void einstellungen_buzzer_aktiv_setzen(bool an);

/* Kalender-Adresse: liefert den manuell gesetzten Override, falls vorhanden,
 * sonst KALENDER_ICS_URL aus secrets.h. puffer wird immer nullterminiert. */
void einstellungen_kalender_url_effektiv(char *puffer, size_t puffer_groesse);
/* Leerer String loescht einen vorhandenen Override wieder (zurueck auf
 * secrets.h-Default). */
void einstellungen_kalender_url_setzen(const char *url);

/* Zuletzt auf dem Hauptbildschirm angezeigter Zeitstempel - Grundlage fuer
 * die Vorbelegung der manuellen Datumseingabe (siehe einrichtung.h), damit
 * man nach einem Stromausfall nicht von 1970 aus rechnen muss. Schreibt
 * intern hoechstens alle 60s tatsaechlich ins NVS (Verschleiss-Bremse). */
time_t einstellungen_letzte_anzeige(void);
void einstellungen_letzte_anzeige_setzen(time_t zeitstempel);

/* Zuletzt erfolgreich per NTP synchronisierter Zeitstempel - rein
 * informativ, ungedrosselt (NTP-Sync ist selten genug). */
time_t einstellungen_letzte_sync(void);
void einstellungen_letzte_sync_setzen(time_t zeitstempel);

#endif
