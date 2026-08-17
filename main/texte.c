#include "texte.h"

#include <stddef.h>

/* Tabelle aller uebersetzbaren Texte. Reihenfolge MUSS zu text_id_t in
 * texte.h passen (der _Static_assert unten prueft nur die Anzahl).
 *
 * WEITERE SPRACHE HINZUFUEGEN:
 *   1. In texte.h einen Wert vor SPRACHE_ANZAHL ergaenzen.
 *   2. Hier je Zeile eine weitere Spalte anfuegen.
 *   3. sprache_name() unten ergaenzen.
 *   4. Pruefen, ob die Schriftarten alle noetigen Zeichen enthalten -
 *      Latin-1 (Umlaute, Accents) ist abgedeckt, alles darueber hinaus
 *      braucht neu erzeugte Fonts (tools/fonts/erzeuge_fonts.ps1).
 *   5. Layout je Bildschirm per Screenshot gegenpruefen: laengere Texte
 *      sind in diesem Projekt schon mehrfach aus Knoepfen und Fenstern
 *      herausgelaufen (FALLSTRICKE #22/#27).
 *
 * NULL bedeutet "nicht uebersetzt" - text() nimmt dann Deutsch. Das ist
 * bewusst erlaubt, damit eine neue Sprache schrittweise wachsen kann, ohne
 * dass zwischendurch leere Knoepfe erscheinen.
 *
 * Umlaute stehen hier bewusst als "ae/oe/ue" wie im gesamten uebrigen
 * Projekt - die erzeugten Fonts enthalten zwar echte Umlaute, aber die
 * Quelldateien bleiben durchgehend ASCII (verhindert Kodierungsaerger
 * zwischen Windows-Editor, Git und Compiler). */
static const char *const TABELLE[TXT_ANZAHL][SPRACHE_ANZAHL] = {
    /* [TEXT_ID]                    = { Deutsch, English } */
    [TXT_WLAN_WECHSELN]             = { "WLAN wechseln", "Change Wi-Fi" },
    [TXT_OFFLINE_WEITER]            = { "Offline weiter", "Continue offline" },

    [TXT_TABLETTEN_HEUTE]           = { "TABLETTEN HEUTE", "PILLS TODAY" },
    [TXT_TERMINE_HEUTE]             = { "TERMINE HEUTE", "EVENTS TODAY" },
    [TXT_HEUTE]                     = { "Heute", "Today" },
    [TXT_KEINE_EINTRAEGE]           = { "-", "-" },
    [TXT_WEITERE]                   = { "+%d weitere", "+%d more" },

    [TXT_VORMITTAG]                 = { "Vormittag", "Morning" },
    [TXT_NACHMITTAG]                = { "Nachmittag", "Afternoon" },
    [TXT_ABEND]                     = { "Abend", "Evening" },
    [TXT_NACHT]                     = { "Nacht", "Night" },

    [TXT_TABLETTE_NEHMEN]           = { "TABLETTE NEHMEN", "TAKE PILL" },
    [TXT_TABLETTEN_NEHMEN]          = { "TABLETTEN NEHMEN", "TAKE PILLS" },
    [TXT_OK]                        = { "OK", "OK" },
    [TXT_ABBRECHEN]                 = { "Abbrechen", "Cancel" },
    [TXT_AKTUALISIERUNG]            = { "AKTUALISIERUNG", "UPDATE" },
    [TXT_UPDATE_BITTE_WARTEN]       = { "Bitte kurz warten - das Geraet startet danach neu",
                                        "Please wait - the device will restart afterwards" },
    [TXT_LAEDT]                     = { "Laedt...", "Loading..." },
    [TXT_KEINE_TABLETTEN_HEUTE]     = { "Keine Tabletten heute.", "No pills today." },
    [TXT_KEINE_TERMINE_HEUTE]       = { "Keine Termine heute.", "No events today." },

    [TXT_EINSTELLUNGEN]             = { "Einstellungen", "Settings" },
    [TXT_SCHLIESSEN]                = { "Schliessen", "Close" },
    [TXT_DATUM_UHRZEIT]             = { "Datum, Uhrzeit einstellen", "Set date and time" },
    [TXT_KALENDER_ADRESSE]          = { "Kalender-Adresse aendern", "Change calendar address" },
    [TXT_DEMO_MODUS]                = { "Demo-Modus", "Demo mode" },
    [TXT_SIGNALTON]                 = { "Signalton bei Erinnerungen", "Sound on reminders" },
    [TXT_SPRACHE]                   = { "Sprache", "Language" },
    [TXT_LAUFENDE_FIRMWARE]         = { "Laufende Firmware: %s", "Running firmware: %s" },
    [TXT_SIGNAL]                    = { "Signal", "Signal" },

    [TXT_SUCHE_NACH_UPDATES]        = { "Suche nach Updates...", "Checking for updates..." },
    [TXT_WLAN_WIRD_VERBUNDEN]       = { "WLAN wird verbunden...", "Connecting to Wi-Fi..." },
    [TXT_KEIN_UPDATE_VERFUEGBAR]    = { "Kein Update verfuegbar", "No update available" },
    [TXT_KEINE_ANDERE_VERSION]      = { "Keine andere Version gefunden.", "No other version found." },
    [TXT_KEINE_VORHERIGE_VERSION]   = { "Keine vorherige Version", "No previous version" },
    [TXT_UPDATE_INSTALLIEREN]       = { "Update auf %s installieren", "Install update to %s" },
    [TXT_SOFORT_ZURUECK]            = { "Sofort zurueck auf %s", "Revert now to %s" },
    [TXT_AUS_DEM_NETZ_LADEN]        = { "Aus dem Netz laden:", "Download version:" },
    [TXT_INSTALLIEREN]              = { "Installieren", "Install" },
    [TXT_KEINE_AUSWAHL]             = { "(keine)", "(none)" },
    [TXT_UPDATE_WIRD_VORBEREITET]   = { "Update wird vorbereitet...", "Preparing update..." },
    [TXT_VERBINDE_MIT_GITHUB]       = { "Verbinde mit GitHub (Versuch %d von %d)...",
                                        "Connecting to GitHub (attempt %d of %d)..." },
    [TXT_UPDATE_FEHLGESCHLAGEN]     = { "Update fehlgeschlagen - Geraet laeuft unveraendert weiter",
                                        "Update failed - device continues unchanged" },
    [TXT_KEINE_VERBINDUNG_GITHUB]   = { "Keine Verbindung zu GitHub - bitte spaeter erneut versuchen",
                                        "No connection to GitHub - please try again later" },

    [TXT_WEB_EINSCHALTEN]           = { "Weboberflaeche einschalten", "Turn on web interface" },
    [TXT_WEB_AUSSCHALTEN]           = { "Weboberflaeche ausschalten", "Turn off web interface" },
    [TXT_WEB_AN_ADRESSE]            = { "Weboberflaeche an - Kalender-Adresse aendern unter:\n"
                                        "Handy: http://seniorenuhr.local/\n"
                                        "Windows-PC: http://%s/",
                                        "Web interface on - change calendar address at:\n"
                                        "Phone: http://seniorenuhr.local/\n"
                                        "Windows PC: http://%s/" },
    [TXT_WEB_AN_KEIN_WLAN]          = { "Weboberflaeche an, aber noch kein WLAN - Adresse erscheint hier, sobald verbunden.",
                                        "Web interface on, but no Wi-Fi yet - the address appears here once connected." },
    [TXT_WEB_AUS_HINWEIS]           = { "Weboberflaeche aus - zum Aendern der Kalender-Adresse per Browser "
                                        "kurz einschalten (kostet Speicher, deshalb nicht dauerhaft an).",
                                        "Web interface off - turn on briefly to change the calendar address in a "
                                        "browser (it uses memory, so it does not stay on)." },

    [TXT_SCREENSHOT_EINSCHALTEN]    = { "Screenshot-Werkzeug einschalten", "Turn on screenshot tool" },
    [TXT_SCREENSHOT_AUSSCHALTEN]    = { "Screenshot-Werkzeug ausschalten", "Turn off screenshot tool" },
    [TXT_SCREENSHOT_HINWEIS]        = { "Fuer Fehlersuche/Dokumentation: Knopf unten mittig auf dem Bildschirm "
                                        "nimmt ein Bildschirmfoto auf, ausgegeben ueber die serielle USB-Verbindung.",
                                        "For debugging/documentation: the button at the bottom centre takes a "
                                        "screenshot, sent over the serial USB connection." },

    [TXT_WLAN_EINRICHTEN]           = { "WLAN einrichten", "Set up Wi-Fi" },
    [TXT_NETZ_WAEHLEN]              = { "Netz waehlen", "Select network" },
    [TXT_PASSWORT]                  = { "Passwort", "Password" },
    [TXT_PASSWORT_BEKANNT]          = { "Passwort (bekannt)", "Password (known)" },
    [TXT_SPEICHERN]                 = { "Speichern", "Save" },
    [TXT_ZURUECK]                   = { "Zurueck", "Back" },
    [TXT_SUCHE_NETZE]               = { "Suche Netze...", "Scanning..." },

    [TXT_DATUM_EINSTELLEN]          = { "Datum und Uhrzeit", "Date and time" },
    [TXT_UEBERNEHMEN]               = { "Uebernehmen", "Apply" },

    [TXT_DIAGNOSE_TITEL]            = { "Neustart nach Stoerung", "Restart after a fault" },
    [TXT_DIAGNOSE_BESTAETIGEN]      = { "Zum Fortfahren Bildschirm beruehren", "Touch the screen to continue" },
};

_Static_assert(sizeof TABELLE / sizeof TABELLE[0] == TXT_ANZAHL,
               "Tabelle in texte.c und text_id_t in texte.h sind nicht mehr gleich lang");

static sprache_t s_sprache = SPRACHE_DEUTSCH;

const char *text(text_id_t id)
{
    if (id < 0 || id >= TXT_ANZAHL)
        return "";
    const char *gewaehlt = TABELLE[id][s_sprache];
    if (gewaehlt)
        return gewaehlt;
    /* Nicht uebersetzt -> Deutsch als Referenzsprache. Lieber ein deutscher
     * Text als eine leere Flaeche; wer sie sieht, weiss sofort, was fehlt. */
    const char *deutsch = TABELLE[id][SPRACHE_DEUTSCH];
    return deutsch ? deutsch : "";
}

sprache_t sprache_aktuell(void) { return s_sprache; }

void sprache_setzen(sprache_t sprache)
{
    if (sprache >= 0 && sprache < SPRACHE_ANZAHL)
        s_sprache = sprache;
}

const char *sprache_name(sprache_t sprache)
{
    switch (sprache) {
    case SPRACHE_DEUTSCH:  return "Deutsch";
    case SPRACHE_ENGLISCH: return "English";
    default:               return "?";
    }
}
