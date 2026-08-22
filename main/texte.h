/*
 * texte.h — Alle auf dem BILDSCHIRM sichtbaren Texte, uebersetzbar.
 *
 * Anlass (10.08.2026): Das Projekt soll oeffentlich werden (GPLv3) und ein
 * Link an Waveshare gehen. Damit Aussenstehende ueberhaupt hineinfinden,
 * gibt es die Oberflaeche zusaetzlich auf Englisch. WEITERE SPRACHEN BEI
 * BEDARF - dafuer reicht eine weitere Spalte in der Tabelle in texte.c
 * (Hinweise dort). Achtung: alles jenseits von Latin-1 (z. B. Polnisch,
 * Griechisch, Kyrillisch) braucht ausserdem neu erzeugte Fonts, siehe
 * tools/fonts/erzeuge_fonts.ps1.
 *
 * BEWUSSTE ABGRENZUNG - was hier NICHT hineingehoert:
 *   - Log-Ausgaben (ESP_LOGx). Die bleiben deutsch: sie sind das
 *     Diagnosewerkzeug fuer die Entwicklung, nicht Teil der Oberflaeche.
 *     Uebersetzte Logs waeren beim Fehlersuchen aktiv hinderlich.
 *   - Quelltext-Kommentare, Bezeichner, Doku (FAHRPLAN/FALLSTRICKE) - die
 *     Projektsprache bleibt Deutsch.
 *   - Die Weboberflaeche (webkonfig.c) - reines HTML fuer einen Browser auf
 *     einem fremden Geraet, nicht das Touchdisplay. Bleibt vorerst deutsch.
 *   - Sprachneutrale Platzhalter/Symbole ("...", "-", "X", "--:--") - die
 *     stehen weiterhin direkt im Code, eine Uebersetzungstabelle dafuer
 *     waere Aufwand ohne Nutzen.
 *
 * Deutsch ist die REFERENZSPRACHE: bei jeder neuen Zeichenkette zuerst den
 * deutschen Text schreiben, dann uebersetzen. Fehlt eine Uebersetzung, faellt
 * text() automatisch auf Deutsch zurueck, statt eine Luecke anzuzeigen.
 */
#ifndef TEXTE_H
#define TEXTE_H

typedef enum {
    SPRACHE_DEUTSCH = 0,
    SPRACHE_ENGLISCH,
    SPRACHE_ANZAHL,
} sprache_t;

/* Alle uebersetzbaren Texte. Reihenfolge egal, aber die Tabelle in texte.c
 * MUSS dieselbe Reihenfolge haben - ein _Static_assert dort prueft nur die
 * Anzahl, nicht die Zuordnung, also beim Ergaenzen beide Stellen zusammen
 * bearbeiten. */
typedef enum {
    /* Startbildschirm / Boot */
    TXT_WLAN_WECHSELN,
    TXT_OFFLINE_WEITER,
    TXT_VERBINDE_MIT_WLAN,          /* Boot-Statuszeile, siehe auch TXT_WLAN_WIRD_VERBUNDEN */
    TXT_UHRZEIT_WIRD_GEHOLT,
    TXT_WARTE_AUF_WLAN,

    /* Zahnrad-Zwischendialog ("Einstellungen oeffnen?") */
    TXT_EINSTELLUNGEN_OEFFNEN_TITEL,
    TXT_NUR_FUER_WARTUNG,
    TXT_OEFFNEN,

    /* Hauptbildschirm */
    TXT_TABLETTEN_HEUTE,
    TXT_TERMINE_HEUTE,
    TXT_HEUTE,
    TXT_KEINE_EINTRAEGE,
    TXT_WEITERE,            /* "+%d weitere" */

    /* Status-Detailfenster (Tipp auf WLAN/Uhr/Kalender-Symbol) */
    TXT_STATUS_TITEL,
    TXT_NOCH_NIE,
    TXT_VOR_SEKUNDEN,               /* "vor %lds" */
    TXT_VOR_MINUTEN,                /* "vor %ldmin" */
    TXT_VOR_STUNDEN_MINUTEN,        /* "vor %ldh %ldmin" */
    TXT_STATUS_WLAN_VERBUNDEN,      /* "WLAN: %s (%d dBm)\nIP %s" */
    TXT_STATUS_WLAN_GETRENNT,
    TXT_STATUS_ZEIT_SYNC,           /* "Uhrzeit: synchronisiert (%s)" */
    TXT_STATUS_ZEIT_UNBESTAETIGT,   /* "Uhrzeit: nicht bestaetigt\n(letzter Sync %s)" */
    TXT_STATUS_KALENDER_AKTUELL,    /* "Kalender: aktuell (%s)" */
    TXT_STATUS_KALENDER_VERALTET,   /* "Kalender: veraltet\n(letzter Sync %s)" */

    /* Tageszeiten (zeit.c) */
    TXT_VORMITTAG,
    TXT_NACHMITTAG,
    TXT_ABEND,
    TXT_NACHT,

    /* Fenster */
    TXT_TABLETTE_NEHMEN,
    TXT_TABLETTEN_NEHMEN,
    TXT_BITTE_BESTAETIGEN,
    TXT_OK,
    TXT_ABBRECHEN,
    TXT_AKTUALISIERUNG,
    TXT_UPDATE_BITTE_WARTEN,
    TXT_LAEDT,
    TXT_KEINE_TABLETTEN_HEUTE,      /* Heute-Fenster: "Keine Tabletten heute." */
    TXT_KEINE_TERMINE_HEUTE,
    TXT_KEINE_TABLETTEN_KURZ,       /* Tages-Fenster (andere Tage): "Keine Tabletten." */
    TXT_KEINE_TERMINE_KURZ,
    /* Vergangener Tag, zu dem das Protokoll nichts weiss - etwa weil das
     * Geraet aus war. Bewusst NICHT "keine Tabletten": das waere eine
     * Aussage ueber Menschen statt ueber das Geraet (siehe
     * tabletten_protokoll.h). */
    TXT_KEINE_AUFZEICHNUNG,
    TXT_TABLETTEN_SPALTE,           /* Spaltenkopf im Tages-/Heute-Fenster: "TABLETTEN" */
    TXT_TERMINE_SPALTE,
    TXT_HEUTE_GROSS,                /* Kopfzeile des Heute-Fensters: "HEUTE" */

    /* Einstellungen-Menue */
    TXT_EINSTELLUNGEN,
    TXT_SCHLIESSEN,
    TXT_DATUM_UHRZEIT,              /* Knopf im Menue: "Datum, Uhrzeit einstellen" */
    TXT_KALENDER_ADRESSE,
    TXT_DEMO_MODUS,
    TXT_SIGNALTON,
    TXT_SPRACHE,
    TXT_NEUSTART,                   /* Knopf im Menue: startet das Geraet neu */
    TXT_NEUSTART_LAEUFT,            /* Beschriftung desselben Knopfes nach dem Tipp */

    /* Tabletten-Rueckblick (tabletten_protokoll.h) */
    TXT_RUECKBLICK,                 /* Knopf im Menue */
    TXT_RUECKBLICK_TITEL,           /* Ueberschrift des Fensters, mit %d Tagen */
    TXT_RUECKBLICK_BILANZ,          /* "%d von %d genommen" */
    TXT_RUECKBLICK_DAVON,           /* "%d zu spaet, %d vergessen" */
    TXT_RUECKBLICK_NICHT_GENOMMEN,  /* Abschnitts-Ueberschrift */
    TXT_RUECKBLICK_ZU_SPAET,        /* Abschnitts-Ueberschrift */
    TXT_RUECKBLICK_ALLES_GUT,       /* wenn es nichts zu bemaengeln gibt */
    TXT_RUECKBLICK_LEER,            /* wenn noch gar nichts aufgezeichnet wurde */
    TXT_RUECKBLICK_WEITERE,         /* "... und %d weitere" */
    TXT_MINUTEN_KURZ,               /* Einheit in "(+130 min)" */
    TXT_GESTERN_KURZ,               /* Marke vor nachhaengenden Tabletten des Vortags */
    TXT_LAUFENDE_FIRMWARE,

    /* Update-Bereich */
    TXT_SUCHE_NACH_UPDATES,
    TXT_WLAN_WIRD_VERBUNDEN,        /* Statuszeile im Menue, siehe auch TXT_VERBINDE_MIT_WLAN */
    TXT_KEIN_UPDATE_VERFUEGBAR,
    TXT_KEINE_ANDERE_VERSION,
    TXT_KEINE_VORHERIGE_VERSION,
    TXT_UPDATE_INSTALLIEREN,      /* "Update auf %s installieren" */
    TXT_SOFORT_ZURUECK,           /* "Sofort zurueck auf %s" */
    TXT_AUS_DEM_NETZ_LADEN,
    TXT_INSTALLIEREN,
    TXT_KEINE_AUSWAHL,            /* "(keine)" */
    TXT_UPDATE_WIRD_VORBEREITET,
    TXT_VERBINDE_MIT_GITHUB,      /* "Verbinde mit GitHub (Versuch %d von %d)..." */
    TXT_UPDATE_FEHLGESCHLAGEN,
    TXT_KEINE_VERBINDUNG_GITHUB,

    /* Weboberflaeche (nur der Knopf/Hinweis im Menue - die Webseite selbst
     * bleibt deutsch, siehe Abgrenzung oben) */
    TXT_WEB_EINSCHALTEN,
    TXT_WEB_AUSSCHALTEN,
    TXT_WEB_AN_ADRESSE,           /* mehrzeilig, mit %s fuer die IP */
    TXT_WEB_AN_KEIN_WLAN,
    TXT_WEB_AUS_HINWEIS,

    /* Screenshot-Werkzeug */
    TXT_SCREENSHOT_EINSCHALTEN,
    TXT_SCREENSHOT_AUSSCHALTEN,
    TXT_SCREENSHOT_HINWEIS,

    /* WLAN-Einrichtung */
    TXT_WLAN_EINRICHTEN,          /* Bildschirmtitel: "WLAN-Zugangsdaten aendern" */
    TXT_SSID_PLATZHALTER,
    TXT_PASSWORT,
    TXT_PASSWORT_BEKANNT,
    TXT_SPEICHERN,
    TXT_ZURUECK,
    TXT_SUCHE_NETZE,
    TXT_WLAN_SIGNAL_NICHT_VERBUNDEN,
    TXT_WLAN_SIGNAL_WERT,          /* "WLAN-Signal: %d dBm (%s)" */
    TXT_GUT,
    TXT_SCHWACH,
    TXT_SEHR_SCHWACH,

    /* Datum/Uhrzeit-Einrichtung */
    TXT_DATUM_EINSTELLEN,         /* Bildschirmtitel: "Datum und Uhrzeit einstellen" */
    TXT_UEBERNEHMEN,
    TXT_KALENDER_URL_PLATZHALTER,
    TXT_TAG,
    TXT_MONAT,
    TXT_JAHR,
    TXT_STUNDE_KURZ,
    TXT_MINUTE_KURZ,

    /* Absturz-Diagnose */
    TXT_DIAGNOSE_TITEL,
    TXT_DIAGNOSE_BESTAETIGEN,
    TXT_VERSTANDEN,
    TXT_ZEIT_NIE_GESETZT,
    TXT_GRUND,                    /* "Grund: %s" */
    TXT_ZULETZT_AKTIV,            /* "Zuletzt aktiv: %s" */
    TXT_ABSTURZ_NUMMER,           /* "Absturz Nr. %lu" */
    TXT_ABSTURZ_PROGRAMMABSTURZ,
    TXT_ABSTURZ_TASK_WATCHDOG,
    TXT_ABSTURZ_INTERRUPT_WATCHDOG,
    TXT_ABSTURZ_WATCHDOG,
    TXT_ABSTURZ_UNTERSPANNUNG,
    TXT_ABSTURZ_UNBEKANNT,

    TXT_ANZAHL
} text_id_t;

/* Liefert den Text in der aktuell eingestellten Sprache. Fehlt die
 * Uebersetzung (NULL in der Tabelle), kommt der deutsche Text zurueck -
 * nie NULL, damit ein vergessener Eintrag die Anzeige nicht zerstoert. */
const char *text(text_id_t id);

/* Aktuelle Sprache; wird beim Start aus dem NVS uebernommen (siehe
 * einstellungen_sprache()). */
sprache_t sprache_aktuell(void);
void sprache_setzen(sprache_t sprache);

/* Anzeigename einer Sprache, IMMER in der jeweiligen Sprache selbst
 * ("Deutsch", "English") - so findet sich auch jemand zurecht, der die
 * gerade eingestellte Sprache nicht lesen kann. */
const char *sprache_name(sprache_t sprache);

#endif
