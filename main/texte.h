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

    /* Hauptbildschirm */
    TXT_TABLETTEN_HEUTE,
    TXT_TERMINE_HEUTE,
    TXT_HEUTE,
    TXT_KEINE_EINTRAEGE,
    TXT_WEITERE,            /* "+%d weitere" */

    /* Tageszeiten (zeit.c) */
    TXT_VORMITTAG,
    TXT_NACHMITTAG,
    TXT_ABEND,
    TXT_NACHT,

    /* Fenster */
    TXT_TABLETTE_NEHMEN,
    TXT_TABLETTEN_NEHMEN,
    TXT_OK,
    TXT_ABBRECHEN,
    TXT_AKTUALISIERUNG,
    TXT_UPDATE_BITTE_WARTEN,
    TXT_LAEDT,
    TXT_KEINE_TABLETTEN_HEUTE,
    TXT_KEINE_TERMINE_HEUTE,

    /* Einstellungen-Menue */
    TXT_EINSTELLUNGEN,
    TXT_SCHLIESSEN,
    TXT_DATUM_UHRZEIT,
    TXT_KALENDER_ADRESSE,
    TXT_DEMO_MODUS,
    TXT_SIGNALTON,
    TXT_SPRACHE,
    TXT_LAUFENDE_FIRMWARE,
    TXT_SIGNAL,

    /* Update-Bereich */
    TXT_SUCHE_NACH_UPDATES,
    TXT_WLAN_WIRD_VERBUNDEN,
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

    /* Weboberflaeche */
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
    TXT_WLAN_EINRICHTEN,
    TXT_NETZ_WAEHLEN,
    TXT_PASSWORT,
    TXT_PASSWORT_BEKANNT,
    TXT_SPEICHERN,
    TXT_ZURUECK,
    TXT_SUCHE_NETZE,

    /* Datum/Uhrzeit-Einrichtung */
    TXT_DATUM_EINSTELLEN,
    TXT_UEBERNEHMEN,

    /* Absturz-Diagnose */
    TXT_DIAGNOSE_TITEL,
    TXT_DIAGNOSE_BESTAETIGEN,

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
