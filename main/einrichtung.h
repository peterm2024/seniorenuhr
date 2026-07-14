/*
 * einrichtung.h — Bildschirme fuer die manuelle Einrichtung, die vom
 * Startbildschirm aus per "WLAN wechseln"/"Offline"-Button erreichbar sind
 * (siehe startbildschirm.h). Beide Bildschirme laufen nicht-blockierend:
 * *_zeigen() baut den Screen auf und kehrt sofort zurueck, app_main fragt
 * per Polling *_status() ab, bis der Benutzer fertig ist.
 */
#ifndef EINRICHTUNG_H
#define EINRICHTUNG_H

typedef enum {
    EINRICHTUNG_OFFEN = 0,
    EINRICHTUNG_UEBERNOMMEN,
    EINRICHTUNG_ABGEBROCHEN,
} einrichtung_status_t;

/* WLAN-Zugangsdaten aendern: SSID/Passwort-Eingabe mit Bildschirmtastatur.
 * "Speichern" schreibt die Daten ins NVS und startet das Geraet neu (kehrt
 * dann nicht zurueck) - der Status wird also nur bei "Abbrechen" jemals
 * EINRICHTUNG_UEBERNOMMEN o.ae., in der Praxis nur ABGEBROCHEN. */
void einrichtung_wlan_zeigen(void);
einrichtung_status_t einrichtung_wlan_status(void);
void einrichtung_wlan_aufraeumen(void);

/* Datum/Uhrzeit manuell setzen (Offline-Betrieb ohne NTP): Walzen fuer
 * Tag/Monat/Jahr/Stunde/Minute. "Uebernehmen" setzt die Systemzeit sofort
 * (siehe zeit_manuell_setzen) und liefert EINRICHTUNG_UEBERNOMMEN. */
void einrichtung_zeit_zeigen(void);
einrichtung_status_t einrichtung_zeit_status(void);
void einrichtung_zeit_aufraeumen(void);

#endif
