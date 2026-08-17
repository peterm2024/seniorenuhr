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
 * startbildschirm.h). Bewusst schlank gehalten - nur Buttons/Schalter, die
 * zu den jeweiligen Unterbildschirmen fuehren (WLAN/Zeit/Kalender-Adresse),
 * kein direkt eingebettetes Textfeld mehr (siehe FALLSTRICKE-Erfahrung:
 * eine lange ICS-URL sprengte das einzeilige Textfeld). "Schliessen"
 * liefert EINRICHTUNG_ABGEBROCHEN (der einzige erreichbare Endzustand -
 * der Buzzer-Schalter wirkt bereits vorher live/sofort). */
typedef enum {
    EINSTELLUNGEN_AKTION_KEINE = 0,
    EINSTELLUNGEN_AKTION_WLAN,
    EINSTELLUNGEN_AKTION_DATUM,
    EINSTELLUNGEN_AKTION_KALENDER_URL,
    /* Vorfuehrung ohne WLAN (z. B. unterwegs an der Powerbank): setzt die
     * Uhrzeit auf einen festen Demo-Zeitstempel und ueberspringt alle noch
     * offenen Boot-Phasen - direkt zur Hauptanzeige, ohne 60s-Countdowns
     * abwarten oder das Datum von Hand einstellen zu muessen (siehe
     * app_main.c). */
    EINSTELLUNGEN_AKTION_DEMO,
    /* Firmware-Update ausdruecklich einspielen bzw. auf die vorherige
     * Version zurueckschalten (siehe ota.h). Beide Buttons erscheinen nur,
     * wenn sie ueberhaupt etwas bewirken koennen - ein Update nur bei
     * gemeldeter neuer Version, das Zurueckschalten nur, wenn die zweite
     * App-Partition ein gueltiges Image enthaelt. */
    EINSTELLUNGEN_AKTION_UPDATE,
    EINSTELLUNGEN_AKTION_VERSION_ZURUECK,
    /* Eine gezielt aus der Auswahlliste gewaehlte Version installieren -
     * welche, liefert einrichtung_einstellungen_gewaehlte_version(). */
    EINSTELLUNGEN_AKTION_VERSION_WAEHLEN,
    /* Zur naechsten Sprache weiterschalten (texte.h) - der Aufrufer baut das
     * Menue danach neu auf, damit alle Beschriftungen sofort wechseln. Wie
     * bei WLAN/Datum/Kalender-Adresse bewusst NICHT direkt im Button-Callback
     * erledigt: der wuerde sonst den gerade aktiven Screen loeschen, waehrend
     * dessen eigener Klick-Handler noch laeuft. */
    EINSTELLUNGEN_AKTION_SPRACHE,
} einstellungen_aktion_t;

/* Nur nach EINSTELLUNGEN_AKTION_VERSION_WAEHLEN aussagekraeftig. */
const char *einrichtung_einstellungen_gewaehlte_version(void);

void einrichtung_einstellungen_zeigen(void);
einrichtung_status_t einrichtung_einstellungen_status(void);
/* Liefert die zuletzt angetippte Aktion und setzt sie danach auf KEINE
 * zurueck (einmal abholen = konsumiert), analog zu
 * startbildschirm_aktion_abfragen(). */
einstellungen_aktion_t einrichtung_einstellungen_aktion_abfragen(void);
void einrichtung_einstellungen_aufraeumen(void);

/* Kalender-Adresse aendern: eigener Bildschirm mit breitem, mehrzeiligem
 * Textfeld (statt einzeilig im Einstellungen-Menue), damit auch eine lange
 * ICS-URL komplett sichtbar bleibt. "Speichern" persistiert den Override
 * (siehe einstellungen.h) und liefert EINRICHTUNG_UEBERNOMMEN. */
void einrichtung_kalenderurl_zeigen(void);
einrichtung_status_t einrichtung_kalenderurl_status(void);
void einrichtung_kalenderurl_aufraeumen(void);

#endif
