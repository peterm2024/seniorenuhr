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
    /* Nur "Offline" (nicht "Offline weiter"): so lautet der tatsaechliche
     * Knopf auf dem Startbildschirm, siehe startbildschirm.c. */
    [TXT_OFFLINE_WEITER]            = { "Offline", "Offline" },
    [TXT_VERBINDE_MIT_WLAN]         = { "Verbinde mit WLAN...", "Connecting to Wi-Fi..." },
    [TXT_UHRZEIT_WIRD_GEHOLT]       = { "Uhrzeit wird geholt...", "Fetching time..." },
    [TXT_WARTE_AUF_WLAN]            = { "Warte auf WLAN...", "Waiting for Wi-Fi..." },

    [TXT_EINSTELLUNGEN_OEFFNEN_TITEL] = { "Einstellungen oeffnen?", "Open settings?" },
    [TXT_NUR_FUER_WARTUNG]          = { "Nur fuer die Wartung gedacht.", "For maintenance use only." },
    [TXT_OEFFNEN]                   = { "Oeffnen", "Open" },

    [TXT_TABLETTEN_HEUTE]           = { "TABLETTEN HEUTE", "PILLS TODAY" },
    [TXT_TERMINE_HEUTE]             = { "TERMINE HEUTE", "EVENTS TODAY" },
    [TXT_HEUTE]                     = { "Heute", "Today" },
    [TXT_KEINE_EINTRAEGE]           = { "-", "-" },
    [TXT_WEITERE]                   = { "+%d weitere", "+%d more" },

    [TXT_STATUS_TITEL]              = { "STATUS", "STATUS" },
    [TXT_NOCH_NIE]                  = { "noch nie", "never" },
    [TXT_VOR_SEKUNDEN]              = { "vor %lds", "%ld s ago" },
    [TXT_VOR_MINUTEN]               = { "vor %ldmin", "%ld min ago" },
    [TXT_VOR_STUNDEN_MINUTEN]       = { "vor %ldh %ldmin", "%ld h %ld min ago" },
    [TXT_STATUS_WLAN_VERBUNDEN]     = { "WLAN: %s (%d dBm)\nIP %s", "Wi-Fi: %s (%d dBm)\nIP %s" },
    [TXT_STATUS_WLAN_GETRENNT]      = { "WLAN: nicht verbunden", "Wi-Fi: not connected" },
    [TXT_STATUS_ZEIT_SYNC]          = { "Uhrzeit: synchronisiert (%s)", "Time: synchronised (%s)" },
    [TXT_STATUS_ZEIT_UNBESTAETIGT]  = { "Uhrzeit: nicht bestaetigt\n(letzter Sync %s)",
                                        "Time: not confirmed\n(last sync %s)" },
    [TXT_STATUS_KALENDER_AKTUELL]   = { "Kalender: aktuell (%s)", "Calendar: up to date (%s)" },
    [TXT_STATUS_KALENDER_VERALTET]  = { "Kalender: veraltet\n(letzter Sync %s)",
                                        "Calendar: outdated\n(last sync %s)" },

    [TXT_VORMITTAG]                 = { "Vormittag", "Morning" },
    [TXT_NACHMITTAG]                = { "Nachmittag", "Afternoon" },
    [TXT_ABEND]                     = { "Abend", "Evening" },
    [TXT_NACHT]                     = { "Nacht", "Night" },

    [TXT_TABLETTE_NEHMEN]           = { "TABLETTE NEHMEN", "TAKE PILL" },
    [TXT_TABLETTEN_NEHMEN]          = { "TABLETTEN NEHMEN", "TAKE PILLS" },
    [TXT_BITTE_BESTAETIGEN]         = { "Bitte bestaetigen", "Please confirm" },
    [TXT_OK]                        = { "OK", "OK" },
    [TXT_ABBRECHEN]                 = { "Abbrechen", "Cancel" },
    [TXT_AKTUALISIERUNG]            = { "AKTUALISIERUNG", "UPDATE" },
    [TXT_UPDATE_BITTE_WARTEN]       = { "Bitte kurz warten - das Geraet startet danach neu",
                                        "Please wait - the device will restart afterwards" },
    [TXT_LAEDT]                     = { "Laedt...", "Loading..." },
    [TXT_KEINE_TABLETTEN_HEUTE]     = { "Keine Tabletten heute.", "No pills today." },
    [TXT_KEINE_TERMINE_HEUTE]       = { "Keine Termine heute.", "No events today." },
    [TXT_KEINE_TABLETTEN_KURZ]      = { "Keine Tabletten.", "No pills." },
    [TXT_KEINE_TERMINE_KURZ]        = { "Keine Termine.", "No events." },
    [TXT_TABLETTEN_SPALTE]          = { "TABLETTEN", "PILLS" },
    [TXT_TERMINE_SPALTE]            = { "TERMINE", "EVENTS" },
    [TXT_HEUTE_GROSS]               = { "HEUTE", "TODAY" },

    [TXT_EINSTELLUNGEN]             = { "Einstellungen", "Settings" },
    [TXT_SCHLIESSEN]                = { "Schliessen", "Close" },
    [TXT_DATUM_UHRZEIT]             = { "Datum, Uhrzeit einstellen", "Set date and time" },
    [TXT_KALENDER_ADRESSE]          = { "Kalender-Adresse aendern", "Change calendar address" },
    [TXT_DEMO_MODUS]                = { "Demo-Modus", "Demo mode" },
    [TXT_SIGNALTON]                 = { "Signalton bei Erinnerungen", "Sound on reminders" },
    [TXT_SPRACHE]                   = { "Sprache", "Language" },
    [TXT_NEUSTART]                  = { "Neustart", "Restart" },
    [TXT_NEUSTART_LAEUFT]           = { "Neustart...", "Restarting..." },

    [TXT_RUECKBLICK]                = { "Tabletten-Rueckblick", "Medication history" },
    [TXT_RUECKBLICK_TITEL]          = { "Letzte %d Tage", "Last %d days" },
    [TXT_RUECKBLICK_BILANZ]         = { "%d von %d genommen", "%d of %d taken" },
    [TXT_RUECKBLICK_DAVON]          = { "%d zu spaet, %d vergessen", "%d late, %d missed" },
    [TXT_RUECKBLICK_NICHT_GENOMMEN] = { "NICHT GENOMMEN", "MISSED" },
    [TXT_RUECKBLICK_ZU_SPAET]       = { "DEUTLICH ZU SPAET", "CLEARLY LATE" },
    [TXT_RUECKBLICK_ALLES_GUT]      = { "Nichts zu beanstanden.", "Nothing to report." },
    [TXT_RUECKBLICK_LEER]           = { "Noch nichts aufgezeichnet.\nDer erste Tag erscheint hier nach Mitternacht.",
                                        "Nothing recorded yet.\nThe first day appears here after midnight." },
    [TXT_RUECKBLICK_WEITERE]        = { "... und %d weitere", "... and %d more" },
    [TXT_MINUTEN_KURZ]              = { "Min", "min" },
    [TXT_GESTERN_KURZ]              = { "Gestern", "Yesterday" },
    [TXT_LAUFENDE_FIRMWARE]         = { "Laufende Firmware: %s", "Running firmware: %s" },

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

    [TXT_WLAN_EINRICHTEN]           = { "WLAN-Zugangsdaten aendern", "Change Wi-Fi credentials" },
    [TXT_SSID_PLATZHALTER]          = { "Netzwerkname (SSID)", "Network name (SSID)" },
    [TXT_PASSWORT]                  = { "Passwort", "Password" },
    [TXT_PASSWORT_BEKANNT]          = { "Passwort (bekannt)", "Password (known)" },
    [TXT_SPEICHERN]                 = { "Speichern", "Save" },
    [TXT_ZURUECK]                   = { "Zurueck", "Back" },
    [TXT_SUCHE_NETZE]               = { "Suche Netzwerke...", "Scanning..." },
    [TXT_WLAN_SIGNAL_NICHT_VERBUNDEN] = { "WLAN-Signal: nicht verbunden", "Wi-Fi signal: not connected" },
    [TXT_WLAN_SIGNAL_WERT]          = { "WLAN-Signal: %d dBm (%s)", "Wi-Fi signal: %d dBm (%s)" },
    [TXT_GUT]                       = { "gut", "good" },
    [TXT_SCHWACH]                   = { "schwach", "weak" },
    [TXT_SEHR_SCHWACH]              = { "sehr schwach", "very weak" },

    [TXT_DATUM_EINSTELLEN]          = { "Datum und Uhrzeit einstellen", "Set date and time" },
    [TXT_UEBERNEHMEN]               = { "Uebernehmen", "Apply" },
    [TXT_KALENDER_URL_PLATZHALTER]  = { "Kalender-Adresse (ICS-URL)", "Calendar address (ICS URL)" },
    [TXT_TAG]                       = { "Tag", "Day" },
    [TXT_MONAT]                     = { "Monat", "Month" },
    [TXT_JAHR]                      = { "Jahr", "Year" },
    [TXT_STUNDE_KURZ]               = { "Std", "Hr" },
    [TXT_MINUTE_KURZ]               = { "Min", "Min" },

    [TXT_DIAGNOSE_TITEL]            = { "NEUSTART NACH FEHLER", "RESTART AFTER A FAULT" },
    [TXT_DIAGNOSE_BESTAETIGEN]      = { "Bitte abfotografieren, dann bestaetigen.",
                                        "Please take a photo, then confirm." },
    [TXT_VERSTANDEN]                = { "Verstanden", "Understood" },
    [TXT_ZEIT_NIE_GESETZT]          = { "unbekannt (Zeit nie gesetzt)", "unknown (time never set)" },
    [TXT_GRUND]                     = { "Grund: %s", "Reason: %s" },
    [TXT_ZULETZT_AKTIV]             = { "Zuletzt aktiv: %s", "Last active: %s" },
    [TXT_ABSTURZ_NUMMER]            = { "Absturz Nr. %lu", "Crash no. %lu" },
    [TXT_ABSTURZ_PROGRAMMABSTURZ]   = { "Programmabsturz", "Program crash" },
    [TXT_ABSTURZ_TASK_WATCHDOG]     = { "Haenger (Task-Watchdog)", "Hang (task watchdog)" },
    [TXT_ABSTURZ_INTERRUPT_WATCHDOG] = { "Haenger (Interrupt-Watchdog)", "Hang (interrupt watchdog)" },
    [TXT_ABSTURZ_WATCHDOG]          = { "Watchdog", "Watchdog" },
    [TXT_ABSTURZ_UNTERSPANNUNG]     = { "Unterspannung (Netzteil?)", "Undervoltage (power supply?)" },
    [TXT_ABSTURZ_UNBEKANNT]         = { "unbekannt", "unknown" },
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
