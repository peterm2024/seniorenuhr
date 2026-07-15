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
 * Tag/Monat/Jahr/Stunde/Minute, vorbelegt mit dem zuletzt angezeigten
 * Zeitstempel (siehe einstellungen.h) statt der rohen Systemzeit - nach
 * einem Stromausfall (Systemzeit dann 1970) muss man so in der Regel nichts
 * bis wenig aendern. "Uebernehmen" setzt die Systemzeit sofort (siehe
 * zeit_manuell_setzen) und liefert EINRICHTUNG_UEBERNOMMEN. */
void einrichtung_zeit_zeigen(void);
einrichtung_status_t einrichtung_zeit_status(void);
void einrichtung_zeit_aufraeumen(void);

/* -------------------------------------------------------------------- */
/* Einstellungen-Menue                                                   */
/* -------------------------------------------------------------------- */

/* Vom Zahnrad-Symbol auf dem Startbildschirm aus erreichbar (siehe
 * startbildschirm.h). Ein Bildschirm mit: Buttons zu den beiden obigen
 * Bildschirmen (WLAN/Zeit), Schalter fuer Signalton, Textfeld fuer die
 * Kalender-Adresse. "Schliessen" liefert EINRICHTUNG_ABGEBROCHEN (der
 * einzige erreichbare Endzustand - Buzzer/Kalender-Aenderungen wirken
 * bereits vorher live/sofort). */
typedef enum {
    EINSTELLUNGEN_AKTION_KEINE = 0,
    EINSTELLUNGEN_AKTION_WLAN,
    EINSTELLUNGEN_AKTION_DATUM,
} einstellungen_aktion_t;

void einrichtung_einstellungen_zeigen(void);
einrichtung_status_t einrichtung_einstellungen_status(void);
/* Liefert die zuletzt angetippte Aktion und setzt sie danach auf KEINE
 * zurueck (einmal abholen = konsumiert), analog zu
 * startbildschirm_aktion_abfragen(). */
einstellungen_aktion_t einrichtung_einstellungen_aktion_abfragen(void);
void einrichtung_einstellungen_aufraeumen(void);

#endif
