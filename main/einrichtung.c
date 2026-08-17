#include "einrichtung.h"
#include "einstellungen.h"
#include "netz.h"
#include "ota.h"
#include "screenshot_debug.h"
#include "texte.h"
#include "webkonfig.h"
#include "zeit.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_lvgl_port.h"
#include "esp_timer.h"

LV_FONT_DECLARE(schrift_mittel_40);
LV_FONT_DECLARE(schrift_klein_28);

/* -------------------------------------------------------------------- */
/* WLAN-Zugangsdaten                                                     */
/* -------------------------------------------------------------------- */

static lv_obj_t *s_wlan_screen;
static lv_obj_t *s_ssid_ta;
static lv_obj_t *s_pass_ta;
static lv_obj_t *s_wlan_keyboard;
static lv_obj_t *s_wlan_dropdown;
static lv_timer_t *s_wlan_scan_timer;
static netz_scan_eintrag_t s_wlan_scan_ergebnisse[NETZ_SCAN_MAX];
static int s_wlan_scan_anzahl;
static volatile einrichtung_status_t s_wlan_status = EINRICHTUNG_OFFEN;
/* true, solange im Passwortfeld noch der Platzhalter eines bereits bekannten
 * Netzes steht und der Benutzer ihn nicht angetastet hat - dann wird beim
 * Speichern NULL uebergeben ("gespeichertes Passwort behalten", siehe netz.h)
 * statt der Fuellzeichen selbst. Peters Wunsch (09.08.2026): zwischen
 * bekannten Netzen umschalten, ohne das Passwort auf dem Touchscreen neu
 * einzutippen. */
static bool s_wlan_pass_ist_platzhalter;
/* Bewusst KEINE echten Sternchen als Inhalt, sondern harmloser Fuelltext: das
 * Feld laeuft im Passwort-Modus, LVGL maskiert also ohnehin jedes Zeichen.
 * Feste 12 Zeichen, damit die Laenge nichts ueber das echte Passwort verraet. */
#define WLAN_PASS_PLATZHALTER "############"

static void wlan_textarea_fokus_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    /* Erster Griff ins Passwortfeld leert den Platzhalter - sonst haengt der
     * neu getippte Text hinten an den Fuellzeichen und ergaebe ein Passwort,
     * das niemand gemeint hat. */
    if (ta == s_pass_ta && s_wlan_pass_ist_platzhalter) {
        s_wlan_pass_ist_platzhalter = false;
        lv_textarea_set_text(s_pass_ta, "");
    }
    lv_keyboard_set_textarea(s_wlan_keyboard, ta);
}

/* true, wenn unsere schlanke Schriftart (siehe tools/fonts/erzeuge_fonts.ps1
 * - ASCII + deutsche Umlaute/Sonderzeichen, kein voller Unicode-Umfang)
 * diesen Codepoint enthaelt. */
static bool codepoint_unterstuetzt(uint32_t cp)
{
    if (cp >= 0x20 && cp <= 0x7E)
        return true;
    switch (cp) {
    case 0xC4: case 0xD6: case 0xDC: case 0xDF:
    case 0xE4: case 0xF6: case 0xFC: case 0xB0:
    case 0x2013: case 0x2019: case 0x201C: case 0x201D:
    case 0x201E: case 0x20AC:
        return true;
    default:
        return false;
    }
}

/* Liest genau ein UTF-8-Zeichen ab roh[0] (hoechstens `rest` Bytes lang),
 * liefert den Codepoint und die dabei verbrauchte Byte-Anzahl (mindestens 1,
 * auch bei einer ungueltigen/abgeschnittenen Sequenz - dann wird einfach ein
 * Byte uebersprungen statt die restliche Zeichenkette zu verschieben). */
static uint32_t utf8_decode(const unsigned char *roh, size_t rest, size_t *verbraucht)
{
    unsigned char b0 = roh[0];
    if (b0 < 0x80) {
        *verbraucht = 1;
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0 && rest >= 2 && (roh[1] & 0xC0) == 0x80) {
        *verbraucht = 2;
        return ((uint32_t)(b0 & 0x1F) << 6) | (roh[1] & 0x3F);
    }
    if ((b0 & 0xF0) == 0xE0 && rest >= 3 && (roh[1] & 0xC0) == 0x80 && (roh[2] & 0xC0) == 0x80) {
        *verbraucht = 3;
        return ((uint32_t)(b0 & 0x0F) << 12) | ((uint32_t)(roh[1] & 0x3F) << 6) | (roh[2] & 0x3F);
    }
    if ((b0 & 0xF8) == 0xF0 && rest >= 4 && (roh[1] & 0xC0) == 0x80 &&
        (roh[2] & 0xC0) == 0x80 && (roh[3] & 0xC0) == 0x80) {
        *verbraucht = 4;
        return ((uint32_t)(b0 & 0x07) << 18) | ((uint32_t)(roh[1] & 0x3F) << 12) |
               ((uint32_t)(roh[2] & 0x3F) << 6) | (roh[3] & 0x3F);
    }
    *verbraucht = 1;
    return 0xFFFD; /* ungueltige Sequenz - wird sowieso als "?" dargestellt */
}

/* Fuer die Anzeige in der Dropdown-Liste: Zeichen, die unsere Schriftart
 * nicht enthaelt (z. B. Emoji oder andere Sprachen in einem Nachbar-WLAN-
 * Namen), werden durch "?" ersetzt - sonst wuerden sie als kaputt wirkende
 * Zeichenfolge dargestellt (siehe Rueckmeldung: "das mit den SSIDs klappt
 * manchmal"). Der gespeicherte Original-SSID (s_wlan_scan_ergebnisse[].ssid)
 * bleibt unveraendert - beim Verbinden zaehlen die echten Bytes, nicht die
 * bereinigte Anzeige. Deutsche Umlaute bleiben dabei als echtes Zeichen
 * erhalten (nicht nur ASCII durchgelassen), da unsere Schriftart sie
 * unterstuetzt. */
static void ssid_anzeige_bereinigen(const char *roh, char *ziel, size_t ziel_groesse)
{
    size_t laenge = strlen(roh);
    size_t i = 0, o = 0;
    while (i < laenge && o + 1 < ziel_groesse) {
        size_t verbraucht;
        uint32_t cp = utf8_decode((const unsigned char *)roh + i, laenge - i, &verbraucht);
        if (codepoint_unterstuetzt(cp)) {
            if (o + verbraucht + 1 > ziel_groesse)
                break;
            memcpy(ziel + o, roh + i, verbraucht);
            o += verbraucht;
        } else {
            ziel[o++] = '?';
        }
        i += verbraucht;
    }
    ziel[o] = '\0';
}

/* Auswahl aus der Dropdown-Liste der gefundenen Netzwerke uebernimmt den
 * SSID-Namen in die normale Textarea - die restliche Logik (Speichern,
 * Passwort-Eingabe) bleibt dadurch unveraendert, versteckte/nicht gefundene
 * Netze lassen sich weiterhin per Hand eintippen. Auswahl per Index statt
 * per String-Vergleich, damit der Platzhaltertext ("Suche Netzwerke..."/
 * "Keine Netzwerke gefunden") nie versehentlich als SSID uebernommen wird.
 *
 * WICHTIG: hier wird die BEREINIGTE Anzeige-Version in die Textarea
 * geschrieben, nicht die rohen Original-Bytes. Ein Nachbar-Netz kann einen
 * SSID mit ungueltigen/unvollstaendigen UTF-8-Sequenzen haben (WLAN-SSIDs
 * sind laut Standard nur ein opaker Byte-String, keine garantiert gueltige
 * Zeichenkodierung) - landet so etwas roh in einem gerenderten LVGL-Textfeld,
 * kann LVGLs UTF-8-Dekodierung beim spaeteren Neuzeichnen/Loeschen daran
 * haengen bleiben (live beobachtet: Board fror beim Schliessen des
 * Bildschirms komplett ein, nachdem so ein Eintrag ausgewaehlt wurde). Wer
 * wirklich exakt diesen Byte-fuer-Byte-Namen verbinden will, muss ihn von
 * Hand eintippen - fuer alle normalen Faelle (eigene/bekannte Netze, immer
 * reines ASCII) aendert die Bereinigung ohnehin nichts. */
static void wlan_dropdown_geaendert_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    uint16_t index = lv_dropdown_get_selected(dd);
    if (index < (uint16_t)s_wlan_scan_anzahl) {
        char bereinigt[sizeof s_wlan_scan_ergebnisse[0].ssid];
        ssid_anzeige_bereinigen(s_wlan_scan_ergebnisse[index].ssid, bereinigt, sizeof bereinigt);
        lv_textarea_set_text(s_ssid_ta, bereinigt);

        /* Bereits bekanntes Netz: Passwortfeld mit Platzhalter fuellen, damit
         * ein Umschalten ohne Neueingabe moeglich ist. Bewusst gegen die
         * UNBEREINIGTE SSID geprueft - genau die steht auch im NVS. */
        s_wlan_pass_ist_platzhalter = netz_profil_bekannt(s_wlan_scan_ergebnisse[index].ssid);
        lv_textarea_set_text(s_pass_ta, s_wlan_pass_ist_platzhalter ? WLAN_PASS_PLATZHALTER : "");
        lv_textarea_set_placeholder_text(s_pass_ta,
                                          s_wlan_pass_ist_platzhalter ? text(TXT_PASSWORT_BEKANNT) : text(TXT_PASSWORT));
    }
}

/* Zuletzt gesetzter Options-Text des Dropdowns - verhindert, dass der
 * Dauerscan (siehe wlan_scan_tick_cb) die Optionen bei unveraendertem
 * Ergebnis immer wieder neu setzt (und dabei eine gerade geoeffnete Liste
 * grundlos zuklappt). Wird beim Bildschirm-Aufbau geleert. */
static char s_wlan_letzte_optionen[NETZ_SCAN_MAX * 34];

/* Pollt den im Hintergrund laufenden WLAN-Scan (siehe netz_scan_starten) und
 * scannt danach fortlaufend weiter, solange der Bildschirm offen ist:
 * iPhone-Hotspots kuendigen sich im Leerlauf nur sparsam an und werden von
 * einem einzelnen Durchlauf oft verpasst (live beobachtet: "Peters iPhone"
 * stand auf Kanal 6 mit 95% Signal und tauchte trotzdem erst nach sehr
 * langer Zeit auf). Die Ergebnisse werden ueber die Runden vereinigt -
 * einmal gesehen bleibt ein Netz in der Liste, und bestehende Eintraege
 * behalten ihren Index (wlan_dropdown_geaendert_cb schlaegt darueber die
 * unbereinigte SSID nach), Neufunde kommen hinten dazu. */
static void wlan_scan_tick_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!netz_scan_fertig())
        return;

    netz_scan_eintrag_t neu[NETZ_SCAN_MAX];
    int neu_anzahl = netz_scan_ergebnisse(neu, NETZ_SCAN_MAX);
    for (int i = 0; i < neu_anzahl; i++) {
        bool bekannt = false;
        for (int j = 0; j < s_wlan_scan_anzahl; j++) {
            if (strcmp(neu[i].ssid, s_wlan_scan_ergebnisse[j].ssid) == 0) {
                s_wlan_scan_ergebnisse[j].rssi = neu[i].rssi;
                bekannt = true;
                break;
            }
        }
        if (!bekannt && s_wlan_scan_anzahl < NETZ_SCAN_MAX)
            s_wlan_scan_ergebnisse[s_wlan_scan_anzahl++] = neu[i];
    }

    /* Naechste Runde anstossen - fruehestens alle 2s, damit ein sofort
     * fehlschlagender Scan-Start (z. B. waehrend eines parallelen
     * Verbindungsaufbaus liefert esp_wifi_scan_start einen Fehler und
     * netz_scan_fertig bleibt sofort true) nicht im 300ms-Takt Warnungen
     * ins Log spammt. */
    static int64_t s_naechster_scan_us;
    int64_t jetzt_us = esp_timer_get_time();
    if (jetzt_us >= s_naechster_scan_us) {
        s_naechster_scan_us = jetzt_us + 2 * 1000000;
        netz_scan_starten();
    }

    char optionen[NETZ_SCAN_MAX * (sizeof s_wlan_scan_ergebnisse[0].ssid + 1)];
    if (s_wlan_scan_anzahl == 0) {
        snprintf(optionen, sizeof optionen, "%s", text(TXT_SUCHE_NETZE));
    } else {
        size_t pos = 0;
        for (int i = 0; i < s_wlan_scan_anzahl; i++) {
            char bereinigt[sizeof s_wlan_scan_ergebnisse[0].ssid];
            ssid_anzeige_bereinigen(s_wlan_scan_ergebnisse[i].ssid, bereinigt, sizeof bereinigt);
            int n = snprintf(optionen + pos, sizeof optionen - pos, "%s%s",
                              i > 0 ? "\n" : "", bereinigt);
            if (n < 0 || (size_t)n >= sizeof optionen - pos)
                break;
            pos += (size_t)n;
        }
    }
    /* FALLSTRICK: lv_dropdown_set_options() gibt den internen Options-Puffer
     * frei und legt einen neuen an - das Label der GEOEFFNETEN Liste zeigt
     * aber per lv_label_set_text_static() (lv_dropdown.c/lv_dropdown_open)
     * direkt auf den ALTEN Puffer und wird von set_options nicht
     * aktualisiert. Hat der Benutzer die Liste schon offen, waehrend der
     * Scan fertig wird ("Suche Netzwerke..." angetippt), zeigte das Label
     * danach auf freigegebenen Speicher: wirre Zeichen auf dem Bildschirm,
     * Absturz/Haenger beim Schliessen. Deshalb: offene Liste vor dem
     * Optionen-Tausch schliessen. (Bewusst KEIN automatisches
     * Wieder-Oeffnen aus dem Timer heraus - so wenige Widget-Manipulationen
     * wie moeglich im Timer-Kontext, siehe FALLSTRICKE #16; der Benutzer
     * tippt die Liste einfach erneut an und sieht dann die Netzwerke.)
     *
     * Optionen nur bei tatsaechlicher Aenderung setzen - der Dauerscan
     * liefert meist dasselbe Ergebnis wie die Runde davor, und ein
     * unnoetiger Optionen-Tausch wuerde eine gerade offene Liste jedes Mal
     * zuklappen. Der Timer laeuft bewusst weiter (kein lv_timer_pause mehr):
     * einrichtung_wlan_aufraeumen() pausiert ihn beim Schliessen des
     * Bildschirms. */
    if (strcmp(optionen, s_wlan_letzte_optionen) != 0) {
        if (lv_dropdown_is_open(s_wlan_dropdown))
            lv_dropdown_close(s_wlan_dropdown);
        lv_dropdown_set_options(s_wlan_dropdown, optionen);
        snprintf(s_wlan_letzte_optionen, sizeof s_wlan_letzte_optionen, "%s", optionen);
    }
}

static void wlan_speichern_cb(lv_event_t *e)
{
    (void)e;
    const char *ssid = lv_textarea_get_text(s_ssid_ta);
    if (strlen(ssid) == 0)
        return; /* ohne Netzwerkname nichts zu speichern */
    /* Steht im Feld noch der unangetastete Platzhalter, NULL uebergeben -
     * das heisst "gespeichertes Passwort behalten" (netz.h). Sonst landeten
     * die Fuellzeichen als Passwort im NVS. */
    const char *passwort = s_wlan_pass_ist_platzhalter ? NULL : lv_textarea_get_text(s_pass_ta);
    /* Bei Erfolg startet das Geraet hier neu und kehrt nicht zurueck. */
    netz_zugangsdaten_speichern(ssid, passwort);
}

static void wlan_abbrechen_cb(lv_event_t *e)
{
    (void)e;
    s_wlan_status = EINRICHTUNG_ABGEBROCHEN;
}

void einrichtung_wlan_zeigen(void)
{
    lvgl_port_lock(0);
    s_wlan_status = EINRICHTUNG_OFFEN;

    s_wlan_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_wlan_screen, lv_color_black(), 0);
    lv_obj_remove_flag(s_wlan_screen, LV_OBJ_FLAG_SCROLLABLE); /* siehe app_main.c/ui_aufbauen */

    lv_obj_t *titel = lv_label_create(s_wlan_screen);
    lv_label_set_text(titel, text(TXT_WLAN_EINRICHTEN));
    lv_obj_set_style_text_font(titel, &schrift_mittel_40, 0);
    lv_obj_set_style_text_color(titel, lv_color_white(), 0);
    lv_obj_align(titel, LV_ALIGN_TOP_MID, 0, 15);

    /* Dropdown mit den gefundenen Netzwerken - fuellt beim Auswaehlen nur die
     * SSID-Textarea darunter, ersetzt sie aber nicht: versteckte oder gerade
     * nicht sichtbare Netze lassen sich weiterhin per Hand eintippen. Der
     * Scan laeuft im Hintergrund (netz_scan_starten/wlan_scan_tick_cb), die
     * Liste zeigt bis dahin einen Platzhaltertext. */
    s_wlan_scan_anzahl = 0;
    s_wlan_letzte_optionen[0] = '\0'; /* frischer Bildschirm = frische Liste */
    s_wlan_pass_ist_platzhalter = false; /* Feld startet leer, siehe unten */
    /* Reconnect-Kreislauf anhalten, BEVOR der erste Scan startet - ohne
     * sichtbares bekanntes Netz haengt das Funkmodul sonst dauerhaft in
     * einem Verbindungsversuch und jeder Scan schlaegt fehl, die Liste
     * bliebe leer (siehe netz_verbindungsversuche_pausieren in netz.h). */
    netz_verbindungsversuche_pausieren(true);
    netz_scan_starten();

    s_wlan_dropdown = lv_dropdown_create(s_wlan_screen);
    lv_dropdown_set_options(s_wlan_dropdown, text(TXT_SUCHE_NETZE));
    lv_obj_set_style_text_font(s_wlan_dropdown, &schrift_klein_28, 0);
    lv_obj_set_size(s_wlan_dropdown, 600, 46);
    lv_obj_align(s_wlan_dropdown, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_add_event_cb(s_wlan_dropdown, wlan_dropdown_geaendert_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Timer nur einmal erzeugen, danach pausieren/fortsetzen - kein
     * delete/create bei Bildschirm-Uebergaengen (siehe FALLSTRICKE #16). */
    if (s_wlan_scan_timer == NULL)
        s_wlan_scan_timer = lv_timer_create(wlan_scan_tick_cb, 300, NULL);
    else
        lv_timer_resume(s_wlan_scan_timer);

    s_ssid_ta = lv_textarea_create(s_wlan_screen);
    lv_textarea_set_one_line(s_ssid_ta, true);
    lv_textarea_set_placeholder_text(s_ssid_ta, text(TXT_SSID_PLATZHALTER));
    lv_obj_set_style_text_font(s_ssid_ta, &schrift_klein_28, 0);
    lv_obj_set_size(s_ssid_ta, 600, 44);
    lv_obj_align(s_ssid_ta, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_add_event_cb(s_ssid_ta, wlan_textarea_fokus_cb, LV_EVENT_FOCUSED, NULL);

    s_pass_ta = lv_textarea_create(s_wlan_screen);
    lv_textarea_set_one_line(s_pass_ta, true);
    lv_textarea_set_password_mode(s_pass_ta, true);
    lv_textarea_set_placeholder_text(s_pass_ta, text(TXT_PASSWORT));
    lv_obj_set_style_text_font(s_pass_ta, &schrift_klein_28, 0);
    lv_obj_set_size(s_pass_ta, 600, 44);
    lv_obj_align(s_pass_ta, LV_ALIGN_TOP_MID, 0, 148);
    lv_obj_add_event_cb(s_pass_ta, wlan_textarea_fokus_cb, LV_EVENT_FOCUSED, NULL);

    lv_obj_t *btn_speichern = lv_button_create(s_wlan_screen);
    lv_obj_set_size(btn_speichern, 260, 50);
    lv_obj_align(btn_speichern, LV_ALIGN_TOP_LEFT, 30, 198);
    lv_obj_add_event_cb(btn_speichern, wlan_speichern_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l1 = lv_label_create(btn_speichern);
    lv_label_set_text(l1, text(TXT_SPEICHERN));
    lv_obj_set_style_text_font(l1, &schrift_klein_28, 0);
    lv_obj_center(l1);

    lv_obj_t *btn_abbrechen = lv_button_create(s_wlan_screen);
    lv_obj_set_size(btn_abbrechen, 200, 50);
    lv_obj_align(btn_abbrechen, LV_ALIGN_TOP_RIGHT, -30, 198);
    lv_obj_add_event_cb(btn_abbrechen, wlan_abbrechen_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l2 = lv_label_create(btn_abbrechen);
    lv_label_set_text(l2, text(TXT_ABBRECHEN));
    lv_obj_set_style_text_font(l2, &schrift_klein_28, 0);
    lv_obj_center(l2);

    /* Tastaturhoehe so gewaehlt, dass ihre Oberkante (480-228=252) unter den
     * Buttons endet (198+50=248) - sonst ueberlappen sich Buttons und
     * Tastatur im unteren Bildschirmdrittel. */
    s_wlan_keyboard = lv_keyboard_create(s_wlan_screen);
    lv_obj_set_size(s_wlan_keyboard, LV_PCT(100), 228);
    lv_obj_align(s_wlan_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(s_wlan_keyboard, s_ssid_ta);

    /* BEKANNTER FEHLER (gefunden 10.08.2026, noch nicht behoben): der Titel
     * "WLAN-Zugangsdaten aendern" erscheint oben abgeschnitten - per
     * Screenshot in Deutsch UND Englisch bestaetigt, also unabhaengig von der
     * Sprache und schon vor der Sprachumstellung vorhanden. Ein Versuch,
     * einen vermuteten Auto-Scroll durch lv_keyboard_set_textarea() mit
     * lv_obj_scroll_to(s_wlan_screen, 0, 0, LV_ANIM_OFF) zurueckzusetzen,
     * behob es NICHT - die eigentliche Ursache ist also etwas anderes.
     * Naechster Schritt: mit dem Screenshot-Werkzeug systematisch eingrenzen,
     * ab welchem Konstruktionsschritt der Versatz entsteht (Titel sofort nach
     * lv_obj_align noch bei y=15 pruefen, dann nach jedem weiteren Element). */
    lv_screen_load(s_wlan_screen);
    lvgl_port_unlock();
}

einrichtung_status_t einrichtung_wlan_status(void)
{
    return s_wlan_status;
}

void einrichtung_wlan_aufraeumen(void)
{
    netz_verbindungsversuche_pausieren(false); /* Gegenstueck zu einrichtung_wlan_zeigen */
    lvgl_port_lock(0);
    if (s_wlan_scan_timer)
        lv_timer_pause(s_wlan_scan_timer); /* pausieren statt loeschen, siehe FALLSTRICKE #16 */
    if (s_wlan_screen) {
        lv_obj_delete(s_wlan_screen);
        s_wlan_screen = NULL;
    }
    lvgl_port_unlock();
}

/* -------------------------------------------------------------------- */
/* Datum/Uhrzeit manuell setzen                                          */
/* -------------------------------------------------------------------- */

/* Fester Jahresbereich statt dynamischer Berechnung: ohne batteriegepufferte
 * RTC steht die Systemzeit nach jedem Stromausfall auf 1970 - eine "aktuelles
 * Jahr"-Schaetzung waere dann nutzlos. Bei Bedarf hier einfach erweitern. */
#define ZEIT_JAHR_VON 2025
#define ZEIT_JAHR_BIS 2035

static lv_obj_t *s_zeit_screen;
static lv_obj_t *s_roller_tag;
static lv_obj_t *s_roller_monat;
static lv_obj_t *s_roller_jahr;
static lv_obj_t *s_roller_stunde;
static lv_obj_t *s_roller_minute;
static volatile einrichtung_status_t s_zeit_status = EINRICHTUNG_OFFEN;

static void roller_zahlen_optionen(char *puffer, size_t puffer_groesse, int von, int bis, bool zweistellig)
{
    size_t pos = 0;
    for (int i = von; i <= bis && pos < puffer_groesse; i++) {
        int n = snprintf(puffer + pos, puffer_groesse - pos, zweistellig ? "%02d\n" : "%d\n", i);
        if (n < 0)
            break;
        pos += (size_t)n;
    }
    if (pos > 0 && pos <= puffer_groesse)
        puffer[pos - 1] = '\0'; /* letztes \n entfernen */
}

static lv_obj_t *roller_erzeugen(lv_obj_t *scr, const char *ueberschrift, const char *optionen,
                                  int32_t x_mitte, int32_t breite, uint16_t start_index)
{
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, ueberschrift);
    lv_obj_set_style_text_font(label, &schrift_klein_28, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, x_mitte, 60);

    lv_obj_t *roller = lv_roller_create(scr);
    lv_roller_set_options(roller, optionen, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller, 4);
    lv_obj_set_width(roller, breite);
    lv_obj_set_style_text_align(roller, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(roller, &schrift_klein_28, LV_PART_MAIN);
    lv_obj_set_style_text_font(roller, &schrift_klein_28, LV_PART_SELECTED);
    lv_obj_align(roller, LV_ALIGN_TOP_MID, x_mitte, 100);
    lv_roller_set_selected(roller, start_index, LV_ANIM_OFF);
    return roller;
}

static void zeit_uebernehmen_cb(lv_event_t *e)
{
    (void)e;
    int tag = lv_roller_get_selected(s_roller_tag) + 1;
    int monat = lv_roller_get_selected(s_roller_monat) + 1;
    int jahr = lv_roller_get_selected(s_roller_jahr) + ZEIT_JAHR_VON;
    int stunde = lv_roller_get_selected(s_roller_stunde);
    int minute = lv_roller_get_selected(s_roller_minute);
    zeit_manuell_setzen(tag, monat, jahr, stunde, minute);
    s_zeit_status = EINRICHTUNG_UEBERNOMMEN;
}

static void zeit_abbrechen_cb(lv_event_t *e)
{
    (void)e;
    s_zeit_status = EINRICHTUNG_ABGEBROCHEN;
}

void einrichtung_zeit_zeigen(void)
{
    lvgl_port_lock(0);
    s_zeit_status = EINRICHTUNG_OFFEN;

    s_zeit_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_zeit_screen, lv_color_black(), 0);
    lv_obj_remove_flag(s_zeit_screen, LV_OBJ_FLAG_SCROLLABLE); /* siehe app_main.c/ui_aufbauen */

    lv_obj_t *titel = lv_label_create(s_zeit_screen);
    lv_label_set_text(titel, text(TXT_DATUM_EINSTELLEN));
    lv_obj_set_style_text_font(titel, &schrift_mittel_40, 0);
    lv_obj_set_style_text_color(titel, lv_color_white(), 0);
    lv_obj_align(titel, LV_ALIGN_TOP_MID, 0, 15);

    /* Ausgangswert: zuletzt angezeigter Zeitstempel statt roher Systemzeit -
     * ohne RTC-Batterie steht die Systemzeit nach jedem Stromausfall auf
     * 1970, die letzte Anzeige ist ein deutlich besserer Ausgangspunkt (in
     * der Regel muss man dann nur noch die seither vergangene Zeit
     * nachtragen). Ganz erster Boot ohne je gespeicherten Wert -> Systemzeit. */
    time_t jetzt = einstellungen_letzte_anzeige();
    if (jetzt == 0)
        jetzt = time(NULL);
    struct tm lokal;
    localtime_r(&jetzt, &lokal);
    int start_tag = lokal.tm_mday >= 1 && lokal.tm_mday <= 31 ? lokal.tm_mday - 1 : 0;
    int start_monat = lokal.tm_mon >= 0 && lokal.tm_mon <= 11 ? lokal.tm_mon : 0;
    int jahr_ist = lokal.tm_year + 1900;
    int start_jahr = (jahr_ist >= ZEIT_JAHR_VON && jahr_ist <= ZEIT_JAHR_BIS) ? jahr_ist - ZEIT_JAHR_VON : 0;
    int start_stunde = lokal.tm_hour >= 0 && lokal.tm_hour <= 23 ? lokal.tm_hour : 0;
    int start_minute = lokal.tm_min >= 0 && lokal.tm_min <= 59 ? lokal.tm_min : 0;

    char tag_optionen[128];
    roller_zahlen_optionen(tag_optionen, sizeof tag_optionen, 1, 31, false);
    char jahr_optionen[64];
    roller_zahlen_optionen(jahr_optionen, sizeof jahr_optionen, ZEIT_JAHR_VON, ZEIT_JAHR_BIS, false);
    char stunde_optionen[128];
    roller_zahlen_optionen(stunde_optionen, sizeof stunde_optionen, 0, 23, true);
    char minute_optionen[256];
    roller_zahlen_optionen(minute_optionen, sizeof minute_optionen, 0, 59, true);
    /* Aus zeit_monatsname() aufgebaut statt einer eigenen Kopie der Namen -
     * vorher stand hier eine zweite, leicht abweichende Liste ("Maerz" ohne
     * Umlaut, waehrend zeit.c "März" fuehrt). Eine einzige Quelle schliesst
     * so ein Auseinanderdriften aus und liefert die Namen automatisch in der
     * eingestellten Sprache. */
    char monat_optionen[160];
    size_t pos = 0;
    for (int i = 0; i < 12 && pos < sizeof monat_optionen; i++) {
        int n = snprintf(monat_optionen + pos, sizeof monat_optionen - pos, "%s%s",
                          i > 0 ? "\n" : "", zeit_monatsname(i));
        if (n < 0)
            break;
        pos += (size_t)n;
    }

    /* Spaltenmittelpunkte relativ zur Bildschirmmitte, je nach Breite des
     * Inhalts (der Monatsname "September" braucht deutlich mehr Platz als
     * eine zweistellige Zahl). */
    s_roller_tag = roller_erzeugen(s_zeit_screen, text(TXT_TAG), tag_optionen, -310, 100, start_tag);
    s_roller_monat = roller_erzeugen(s_zeit_screen, text(TXT_MONAT), monat_optionen, -130, 220, start_monat);
    s_roller_jahr = roller_erzeugen(s_zeit_screen, text(TXT_JAHR), jahr_optionen, 60, 120, start_jahr);
    s_roller_stunde = roller_erzeugen(s_zeit_screen, text(TXT_STUNDE_KURZ), stunde_optionen, 190, 100, start_stunde);
    s_roller_minute = roller_erzeugen(s_zeit_screen, text(TXT_MINUTE_KURZ), minute_optionen, 310, 100, start_minute);

    lv_obj_t *btn_uebernehmen = lv_button_create(s_zeit_screen);
    lv_obj_set_size(btn_uebernehmen, 260, 60);
    lv_obj_align(btn_uebernehmen, LV_ALIGN_BOTTOM_MID, -150, -20);
    lv_obj_add_event_cb(btn_uebernehmen, zeit_uebernehmen_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l1 = lv_label_create(btn_uebernehmen);
    lv_label_set_text(l1, text(TXT_UEBERNEHMEN));
    lv_obj_set_style_text_font(l1, &schrift_klein_28, 0);
    lv_obj_center(l1);

    lv_obj_t *btn_abbrechen = lv_button_create(s_zeit_screen);
    lv_obj_set_size(btn_abbrechen, 200, 60);
    lv_obj_align(btn_abbrechen, LV_ALIGN_BOTTOM_MID, 150, -20);
    lv_obj_add_event_cb(btn_abbrechen, zeit_abbrechen_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l2 = lv_label_create(btn_abbrechen);
    lv_label_set_text(l2, text(TXT_ABBRECHEN));
    lv_obj_set_style_text_font(l2, &schrift_klein_28, 0);
    lv_obj_center(l2);

    lv_screen_load(s_zeit_screen);
    lvgl_port_unlock();
}

einrichtung_status_t einrichtung_zeit_status(void)
{
    return s_zeit_status;
}

void einrichtung_zeit_aufraeumen(void)
{
    lvgl_port_lock(0);
    if (s_zeit_screen) {
        lv_obj_delete(s_zeit_screen);
        s_zeit_screen = NULL;
    }
    lvgl_port_unlock();
}

/* -------------------------------------------------------------------- */
/* Einstellungen-Menue                                                   */
/* -------------------------------------------------------------------- */

static lv_obj_t *s_einstellungen_screen;
static volatile einrichtung_status_t s_einstellungen_status = EINRICHTUNG_OFFEN;
static volatile einstellungen_aktion_t s_einstellungen_aktion = EINSTELLUNGEN_AKTION_KEINE;
static lv_obj_t *s_signal_label;
static lv_obj_t *s_signal_bar;
static lv_timer_t *s_signal_timer;
/* Weboberflaeche (webkonfig.c) - seit FALLSTRICKE #39 NUR AUF ZURUF (kostet
 * dauerhaft ca. 17 KB internen SRAM), deshalb hier ein Knopf statt eines
 * blossen Hinweistexts. s_webkonfig_hinweis_label sitzt IM Knopf, dessen Text
 * aendert sich je nach Zustand ("einschalten"/"ausschalten"). */
static lv_obj_t *s_webkonfig_hinweis;
static lv_obj_t *s_webkonfig_knopf_label;
static lv_obj_t *s_firmware_label;    /* darunter, Position haengt von dessen Hoehe ab */
static int32_t s_webkonfig_hinweis_y; /* fest (Knopfhoehe ist konstant), zum Nachmessen der Hoehe */
/* Screenshot-Werkzeug (screenshot_debug.c): urspruenglich nur automatischer
 * Dev-Boot-Start, seit 09.08.2026 zusaetzlich hier schaltbar - auch im
 * Produktions-Build ("Elternmodus"), siehe screenshot_debug.h. Bleibt anders
 * als die Weboberflaeche beim Verlassen des Menues bewusst AN (kein
 * screenshot_debug_stop() in einrichtung_einstellungen_aufraeumen()): das
 * Werkzeug kostet im Leerlauf keinen internen SRAM (nur ein kleiner LVGL-
 * Button aus dem separaten 64-KB-Pool), anders als Webserver+mDNS. */
static lv_obj_t *s_screenshot_knopf_label;
/* Update-Bereich: eigener Container, weil er sich nachtraeglich fuellt,
 * sobald die angestossene Pruefung geantwortet hat. Feste Hoehe, damit
 * nichts darunter verrutscht - Schalter, Signalbalken und Hinweis stehen an
 * fest berechneten Y-Positionen.
 *
 * Bemessen auf den groessten Fall: die Knopfreihe bricht bei beiden Knoepfen
 * ("Update auf ... installieren" + "Sofort zurueck auf ...") auf ZWEI Zeilen
 * um (2 x 64 + 10 Abstand = 138), darunter noch die Versionszeile (64) plus
 * Abstand. Der erste Anlauf mit 150 reichte nur fuer die Knopfreihe - die
 * Versionszeile wurde abgeschnitten und der Schalter darunter ueberlappte
 * sie (per Screenshot aufgefallen). Ist weniger zu zeigen, bleibt hier
 * Leerraum; seit der Bildschirm scrollt, stoert das nicht.
 *
 * Seit 09.08.2026 (Peters Wunsch): die Knoepfe stehen jetzt IMMER da (nur
 * deaktiviert, statt aus dem Nichts aufzutauchen), dazu eine Statuszeile
 * ("WLAN wird verbunden..." etc.) ueber der Knopfreihe - deshalb +48
 * gegenueber dem alten Wert. Knopfreihe/Versionszeile sind dadurch jetzt
 * IMMER gleich hoch (beide Knoepfe immer vorhanden -> immer derselbe
 * Zeilenumbruch), nur der Statustext oben aendert sich noch. Falls die
 * Statuszeile bei einem laengeren Text doch umbricht, per Screenshot
 * gegenpruefen und hier nachjustieren (gleiches Vorgehen wie beim 150->222
 * Sprung oben). */
#define UPDATE_BEREICH_HOEHE 270
static lv_obj_t *s_update_bereich;
static lv_obj_t *s_versionen_dropdown;
static bool s_update_stand_verbunden;
static bool s_update_stand_verfuegbar;
static int s_update_stand_anzahl = -1;
static bool s_update_stand_suche;
/* Gesetzt, solange dem OTA-Task noch ein Anstoss geschuldet ist: beim Aufbau
 * des Menues stand kein WLAN, der Anstoss konnte also nicht raus. Genau der
 * Regelfall beim Start - das Menue ist ueber das Zahnrad schon waehrend der
 * WLAN-Phase erreichbar, also bevor es eine Verbindung gibt. */
static bool s_update_anstoss_offen;

/* Der Update-Bereich steht hier oben, weil ihn der Timer-Callback braucht -
 * die Knopf-Hilfsfunktion und die Aktions-Callbacks folgen erst weiter unten. */
static lv_obj_t *einstellungen_nav_button_erzeugen(lv_obj_t *parent, const char *text,
                                                    lv_event_cb_t cb);
static void einstellungen_update_cb(lv_event_t *e);
static void einstellungen_version_zurueck_cb(lv_event_t *e);
static void einstellungen_version_waehlen_cb(lv_event_t *e);
static void webkonfig_knopf_cb(lv_event_t *e);
static void webkonfig_bereich_aktualisieren(void);
static void screenshot_knopf_cb(lv_event_t *e);

/* Dbm-Spanne, auf die der Balken 0-100% abbildet - -90 dBm (praktisch
 * unbrauchbar) bis -30 dBm (denkbar bestmoeglicher Empfang in Router-Naehe). */
#define SIGNAL_DBM_MIN -90
#define SIGNAL_DBM_MAX -30

/* Bewusst OHNE Balken-Animation (LV_ANIM_OFF) und nur bei tatsaechlicher
 * Aenderung neu gesetzt: eine alle paar hundert Millisekunden neu
 * gestartete lv_anim invalidiert die Anzeige permanent - dauert das
 * Neuzeichnen dann laenger als die kuerzeste Timer-Periode, kommt
 * lv_timer_handler() aus seiner Runden-Schleife nie mehr heraus und die
 * LVGL-Task frisst die CPU komplett auf (Task-Watchdog-Haenger, live per
 * Core-Dump nachgewiesen: handler_start lag 6s vor dem Absturz). Siehe
 * FALLSTRICKE_UND_WORKAROUNDS.md #16. */
static void signal_aktualisieren(void)
{
    int rssi = netz_rssi_dbm();
    /* Heisst bewusst NICHT "text" - das wuerde die gleichnamige
     * Uebersetzungsfunktion text() aus texte.h in dieser Funktion
     * verdecken. */
    char anzeige_text[64];
    int pct = 0;
    lv_color_t farbe;
    if (rssi == 0) {
        snprintf(anzeige_text, sizeof anzeige_text, "%s", text(TXT_WLAN_SIGNAL_NICHT_VERBUNDEN));
        farbe = lv_palette_main(LV_PALETTE_GREY);
    } else {
        const char *guete = rssi >= -67 ? text(TXT_GUT) : (rssi >= -80 ? text(TXT_SCHWACH) : text(TXT_SEHR_SCHWACH));
        snprintf(anzeige_text, sizeof anzeige_text, text(TXT_WLAN_SIGNAL_WERT), rssi, guete);

        pct = (rssi - SIGNAL_DBM_MIN) * 100 / (SIGNAL_DBM_MAX - SIGNAL_DBM_MIN);
        if (pct < 0)
            pct = 0;
        if (pct > 100)
            pct = 100;
        farbe = rssi >= -67 ? lv_palette_main(LV_PALETTE_GREEN)
              : rssi >= -80 ? lv_palette_main(LV_PALETTE_ORANGE)
                            : lv_palette_main(LV_PALETTE_RED);
    }

    if (pct != lv_bar_get_value(s_signal_bar)) {
        lv_bar_set_value(s_signal_bar, pct, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(s_signal_bar, farbe, LV_PART_INDICATOR);
    }
    if (strcmp(anzeige_text, lv_label_get_text(s_signal_label)) != 0)
        lv_label_set_text(s_signal_label, anzeige_text);
}

/* Fuellt den Update-Bereich passend zum aktuellen Wissensstand. Wird beim
 * Aufbau UND spaeter erneut aufgerufen, sobald die angestossene Pruefung
 * geantwortet hat (siehe update_bereich_pruefen). */
static void update_bereich_aufbauen(void)
{
    if (s_update_bereich == NULL)
        return;

    lv_obj_clean(s_update_bereich);
    s_versionen_dropdown = NULL;

    bool verbunden = netz_ist_verbunden();
    bool update_da = ota_update_verfuegbar();
    char vorherige[32];
    bool zurueck_da = ota_vorherige_version(vorherige, sizeof vorherige);
    int anzahl = ota_versionen_anzahl();
    bool suche = ota_pruefung_laeuft();

    /* Statuszeile: sagt WARUM die Knoepfe unten (noch) deaktiviert sind,
     * statt das offen zu lassen - genau die Ratlosigkeit, die dieses Menue
     * vermeiden soll. Peters Wunsch: die Knoepfe selbst stehen jetzt IMMER
     * da (nur deaktiviert), damit sichtbar ist, dass im Hintergrund etwas
     * passiert, statt dass sie ploetzlich aus dem Nichts auftauchen. Leer,
     * sobald nichts mehr zu erklaeren ist. */
    const char *status_text = "";
    if (!verbunden)
        status_text = text(TXT_WLAN_WIRD_VERBUNDEN);
    else if (suche)
        status_text = text(TXT_SUCHE_NACH_UPDATES);
    else if (!update_da && !zurueck_da && anzahl == 0)
        status_text = text(TXT_KEINE_ANDERE_VERSION);

    lv_obj_t *status_label = lv_label_create(s_update_bereich);
    lv_label_set_text(status_label, status_text);
    lv_obj_set_style_text_font(status_label, &schrift_klein_28, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xa0a0a0), 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_LEFT, 0, 8);
    lv_obj_update_layout(status_label);
    int32_t knopf_y = 8 + lv_obj_get_height(status_label) + 10;

    /* Knopfreihe: die beiden Wege, die ohne Auswahl auskommen - IMMER
     * beide Knoepfe, deaktiviert statt weggelassen, wenn (noch) nicht
     * moeglich. */
    lv_obj_t *knopfreihe = lv_obj_create(s_update_bereich);
    lv_obj_remove_style_all(knopfreihe);
    lv_obj_remove_flag(knopfreihe, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(knopfreihe, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(knopfreihe, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(knopfreihe, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(knopfreihe, 14, 0);
    lv_obj_set_style_pad_row(knopfreihe, 10, 0);
    lv_obj_align(knopfreihe, LV_ALIGN_TOP_LEFT, 0, knopf_y);

    char beschriftung[64];
    if (update_da)
        snprintf(beschriftung, sizeof beschriftung, text(TXT_UPDATE_INSTALLIEREN),
                 ota_verfuegbare_version());
    else
        snprintf(beschriftung, sizeof beschriftung, "%s", text(TXT_KEIN_UPDATE_VERFUEGBAR));
    lv_obj_t *update_knopf = einstellungen_nav_button_erzeugen(knopfreihe, beschriftung, einstellungen_update_cb);
    if (!update_da)
        lv_obj_add_state(update_knopf, LV_STATE_DISABLED);

    /* "Zurueck auf X" ist etwas grundlegend anderes als die Auswahlliste: die
     * Version liegt bereits in der zweiten Flash-Partition, es wird nichts
     * heruntergeladen - der Wechsel ist sofort fertig und braucht kein Netz. */
    if (zurueck_da)
        snprintf(beschriftung, sizeof beschriftung, text(TXT_SOFORT_ZURUECK), vorherige);
    else
        snprintf(beschriftung, sizeof beschriftung, "%s", text(TXT_KEINE_VORHERIGE_VERSION));
    lv_obj_t *zurueck_knopf = einstellungen_nav_button_erzeugen(knopfreihe, beschriftung, einstellungen_version_zurueck_cb);
    if (!zurueck_da)
        lv_obj_add_state(zurueck_knopf, LV_STATE_DISABLED);

    lv_obj_update_layout(knopfreihe);
    int32_t zeilen_y = knopf_y + lv_obj_get_height(knopfreihe) + 12;

    lv_obj_t *versionszeile = lv_obj_create(s_update_bereich);
    lv_obj_remove_style_all(versionszeile);
    lv_obj_remove_flag(versionszeile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(versionszeile, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(versionszeile, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(versionszeile, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(versionszeile, 14, 0);
    lv_obj_set_style_pad_row(versionszeile, 10, 0);
    lv_obj_align(versionszeile, LV_ALIGN_TOP_LEFT, 0, zeilen_y);

    /* Kurz halten! Vorher stand hier die komplette laufende Version samt
     * Git-Hash, gefolgt von einem ins Leere laufenden " -". Die Zeile brach
     * dadurch um, schob den Signalbalken halb aus dem Bild und den Hinweis
     * auf die Weboberflaeche ganz heraus (per Screenshot aufgefallen). Die
     * laufende Version steht jetzt unten bei den uebrigen Statusangaben. */
    lv_obj_t *laufend = lv_label_create(versionszeile);
    lv_label_set_text(laufend, text(TXT_AUS_DEM_NETZ_LADEN));
    lv_obj_set_style_text_font(laufend, &schrift_klein_28, 0);
    lv_obj_set_style_text_color(laufend, lv_color_white(), 0);

    s_versionen_dropdown = lv_dropdown_create(versionszeile);
    lv_obj_set_width(s_versionen_dropdown, 200);
    lv_obj_set_style_text_font(s_versionen_dropdown, &schrift_klein_28, 0);
    /* Ohne das zeichnet LVGL sein Standard-Pfeilsymbol, das in der
     * Montserrat-Schrift dieses Projekts fehlt - auf dem Geraet erschien dort
     * ein leeres Kaestchen (per Screenshot aufgefallen). Dieselbe Ursache wie
     * beim fehlenden Haken-Symbol, siehe "[x] "-Praefix bei den Tabletten. */
    lv_dropdown_set_symbol(s_versionen_dropdown, NULL);

    lv_obj_t *installieren_knopf = einstellungen_nav_button_erzeugen(versionszeile, text(TXT_INSTALLIEREN),
                                                                      einstellungen_version_waehlen_cb);
    if (anzahl > 0) {
        /* Optionen einmalig beim Aufbau setzen (nie waehrend die Liste offen
         * ist - siehe der Fallstrick beim WLAN-Dropdown oben). */
        char optionen[OTA_VERSIONEN_MAX * (OTA_VERSION_MAX + 1)];
        size_t pos = 0;
        optionen[0] = '\0';
        for (int i = 0; i < anzahl; i++) {
            int n = snprintf(optionen + pos, sizeof optionen - pos, "%s%s",
                             i ? "\n" : "", ota_version_name(i));
            if (n < 0 || (size_t)n >= sizeof optionen - pos)
                break;
            pos += (size_t)n;
        }
        lv_dropdown_set_options(s_versionen_dropdown, optionen);
    } else {
        lv_dropdown_set_options(s_versionen_dropdown, text(TXT_KEINE_AUSWAHL));
        lv_obj_add_state(s_versionen_dropdown, LV_STATE_DISABLED);
        lv_obj_add_state(installieren_knopf, LV_STATE_DISABLED);
    }
}

/* Baut den Bereich nur neu, wenn sich am Wissensstand etwas geaendert hat -
 * ein Neuaufbau bei jedem Timer-Tick wuerde die geoeffnete Auswahlliste unter
 * dem Finger wegreissen (derselbe Fallstrick wie beim WLAN-Dropdown). */
static void update_bereich_pruefen(void)
{
    /* Nachgeholter Anstoss, sobald das WLAN steht (siehe s_update_anstoss_offen).
     * Nur anstossen, nie selbst telefonieren - wir laufen hier im LVGL-Timer. */
    if (s_update_anstoss_offen && netz_ist_verbunden()) {
        s_update_anstoss_offen = false;
        ota_pruefung_anstossen();
    }

    bool verbunden = netz_ist_verbunden();
    bool update_da = ota_update_verfuegbar();
    int anzahl = ota_versionen_anzahl();
    bool suche = ota_pruefung_laeuft();

    /* "verbunden" mitverglichen, damit die Statuszeile umgehend von "WLAN
     * wird verbunden..." auf "Suche nach Updates..." wechselt, statt erst
     * auf eine der drei anderen Aenderungen zu warten. */
    if (verbunden == s_update_stand_verbunden && update_da == s_update_stand_verfuegbar &&
        anzahl == s_update_stand_anzahl && suche == s_update_stand_suche)
        return;

    s_update_stand_verbunden = verbunden;
    s_update_stand_verfuegbar = update_da;
    s_update_stand_anzahl = anzahl;
    s_update_stand_suche = suche;
    update_bereich_aufbauen();
}

static void signal_tick_cb(lv_timer_t *timer)
{
    (void)timer;
    signal_aktualisieren();
    update_bereich_pruefen();
}

static void einstellungen_wlan_cb(lv_event_t *e)
{
    (void)e;
    s_einstellungen_aktion = EINSTELLUNGEN_AKTION_WLAN;
}

static void einstellungen_datum_cb(lv_event_t *e)
{
    (void)e;
    s_einstellungen_aktion = EINSTELLUNGEN_AKTION_DATUM;
}

static void einstellungen_kalenderurl_cb(lv_event_t *e)
{
    (void)e;
    s_einstellungen_aktion = EINSTELLUNGEN_AKTION_KALENDER_URL;
}

static void einstellungen_demo_cb(lv_event_t *e)
{
    (void)e;
    s_einstellungen_aktion = EINSTELLUNGEN_AKTION_DEMO;
}

static void einstellungen_sprache_cb(lv_event_t *e)
{
    (void)e;
    s_einstellungen_aktion = EINSTELLUNGEN_AKTION_SPRACHE;
}

static void einstellungen_update_cb(lv_event_t *e)
{
    (void)e;
    s_einstellungen_aktion = EINSTELLUNGEN_AKTION_UPDATE;
}

static void einstellungen_version_zurueck_cb(lv_event_t *e)
{
    (void)e;
    s_einstellungen_aktion = EINSTELLUNGEN_AKTION_VERSION_ZURUECK;
}

/* Auswahlliste aller im Download-Repo veroeffentlichten Versionen. Anders
 * als das Zurueckschalten zwischen den zwei App-Partitionen kann hiermit
 * JEDE veroeffentlichte Version geholt werden - sie wird frisch
 * heruntergeladen (Peters Fall: eine Version gefaellt zunaechst nicht,
 * spaeter will man ein Feature daraus dann doch). */
static char s_gewaehlte_version[OTA_VERSION_MAX];

const char *einrichtung_einstellungen_gewaehlte_version(void)
{
    return s_gewaehlte_version;
}

static void einstellungen_version_waehlen_cb(lv_event_t *e)
{
    (void)e;
    if (!s_versionen_dropdown)
        return;
    int index = (int)lv_dropdown_get_selected(s_versionen_dropdown);
    if (index < 0 || index >= ota_versionen_anzahl())
        return;
    snprintf(s_gewaehlte_version, sizeof s_gewaehlte_version, "%s", ota_version_name(index));
    s_einstellungen_aktion = EINSTELLUNGEN_AKTION_VERSION_WAEHLEN;
}

static void einstellungen_schliessen_cb(lv_event_t *e)
{
    (void)e;
    s_einstellungen_status = EINRICHTUNG_ABGEBROCHEN;
}

static void einstellungen_buzzer_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    einstellungen_buzzer_aktiv_setzen(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

/* Fuellt Knopf-Beschriftung und Hinweistext passend zum aktuellen Zustand -
 * beim Aufbau des Menues UND nach jedem Tipp auf den Knopf selbst. Die
 * Firmware-Zeile darunter wird dabei neu positioniert: der Hinweistext hat je
 * nach Zustand (an/aus, IP bekannt/unbekannt) unterschiedlich viele Zeilen,
 * seine Hoehe wird GEMESSEN statt geraten (FALLSTRICKE #22). */
static void webkonfig_bereich_aktualisieren(void)
{
    if (!s_webkonfig_hinweis || !s_webkonfig_knopf_label)
        return;

    if (webkonfig_laeuft()) {
        lv_label_set_text(s_webkonfig_knopf_label, text(TXT_WEB_AUSSCHALTEN));
        char ip_text[16];
        netz_ip_text(ip_text, sizeof ip_text);
        /* Heisst bewusst "hinweis_text", nicht "text" - sonst wuerde die
         * lokale Variable die Uebersetzungsfunktion text() verdecken. */
        char hinweis_text[192];
        if (ip_text[0])
            snprintf(hinweis_text, sizeof hinweis_text, text(TXT_WEB_AN_ADRESSE), ip_text);
        else
            snprintf(hinweis_text, sizeof hinweis_text, "%s", text(TXT_WEB_AN_KEIN_WLAN));
        lv_label_set_text(s_webkonfig_hinweis, hinweis_text);
    } else {
        lv_label_set_text(s_webkonfig_knopf_label, text(TXT_WEB_EINSCHALTEN));
        lv_label_set_text(s_webkonfig_hinweis, text(TXT_WEB_AUS_HINWEIS));
    }

    if (s_firmware_label) {
        lv_obj_update_layout(s_webkonfig_hinweis);
        lv_obj_align(s_firmware_label, LV_ALIGN_TOP_LEFT, 30,
                     s_webkonfig_hinweis_y + lv_obj_get_height(s_webkonfig_hinweis) + 14);
    }
}

static void webkonfig_knopf_cb(lv_event_t *e)
{
    (void)e;
    if (webkonfig_laeuft())
        webkonfig_stop();
    else
        webkonfig_start();
    webkonfig_bereich_aktualisieren();
}

static void screenshot_knopf_cb(lv_event_t *e)
{
    (void)e;
    if (screenshot_debug_laeuft())
        screenshot_debug_stop();
    else
        screenshot_debug_start();
    /* screenshot_debug_stop() verschiebt das Abschalten, solange eine
     * Aufnahme/Uebertragung laeuft (siehe screenshot_debug.h) - deshalb hier
     * den TATSAECHLICHEN Zustand erfragen statt blind umzuschalten. */
    lv_label_set_text(s_screenshot_knopf_label, screenshot_debug_laeuft()
                       ? text(TXT_SCREENSHOT_AUSSCHALTEN) : text(TXT_SCREENSHOT_EINSCHALTEN));
}

/* Breite passt sich per LV_SIZE_CONTENT der Beschriftung an (wie das
 * "Heute"-Button-Muster in tagesansicht.c) - eine geratene Festbreite hatte
 * zuvor laengere Texte ("Datum, Uhrzeit einstellen", "Schliessen")
 * abgeschnitten. */
static lv_obj_t *einstellungen_nav_button_erzeugen(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, LV_SIZE_CONTENT, 48);
    lv_obj_set_style_pad_left(btn, 20, 0);
    lv_obj_set_style_pad_right(btn, 20, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &schrift_klein_28, 0);
    lv_obj_center(label);
    return btn;
}

static lv_obj_t *einstellungen_schalter_zeile(lv_obj_t *scr, int32_t y, const char *text, bool an,
                                               lv_event_cb_t cb)
{
    lv_obj_t *sw = lv_switch_create(scr);
    lv_obj_set_size(sw, 70, 36);
    lv_obj_align(sw, LV_ALIGN_TOP_LEFT, 30, y);
    if (an)
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &schrift_klein_28, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 115, y + 4);
    return sw;
}

void einrichtung_einstellungen_zeigen(void)
{
    lvgl_port_lock(0);
    s_einstellungen_status = EINRICHTUNG_OFFEN;
    s_einstellungen_aktion = EINSTELLUNGEN_AKTION_KEINE;

    /* WICHTIG (FALLSTRICKE #16): Bei der Rueckkehr aus einem Unter-
     * Bildschirm (WLAN/Datum/Kalender-URL) wird diese Funktion erneut
     * aufgerufen - ein evtl. noch vorhandener alter Menue-Screen MUSS
     * vorher geloescht werden, sonst leakt jeder Menue-Durchlauf einen
     * kompletten Screen (~5-10 KB) in den nur 64 KB grossen LVGL-Pool.
     * Nach einer Handvoll Bedien-Runden war der Pool voll und die
     * LVGL-Task blieb beim Zeichnen in einer Endlosschleife haengen
     * (Task-Watchdog). Loeschen ist hier gefahrlos: der alte Menue-Screen
     * ist in diesem Moment nie der aktive Screen (aktiv ist der gerade
     * geschlossene Unter-Bildschirm; die Regel "aktiven Screen nie
     * loeschen" aus FALLSTRICKE #7 bleibt gewahrt). */
    if (s_einstellungen_screen) {
        lv_obj_delete(s_einstellungen_screen);
        s_einstellungen_screen = NULL;
    }

    /* Zeiger auf Kinder des soeben geloeschten Screens ungueltig machen -
     * das Dropdown wird weiter unten nur unter Bedingungen neu angelegt. */
    s_versionen_dropdown = NULL;
    s_webkonfig_hinweis = NULL;
    s_webkonfig_knopf_label = NULL;
    s_firmware_label = NULL;
    s_screenshot_knopf_label = NULL;

    s_einstellungen_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_einstellungen_screen, lv_color_black(), 0);

    /* Anders als der Uhren-Bildschirm (der bewusst unverschiebbar ist, sonst
     * laesst sich die Anzeige versehentlich wegziehen - siehe
     * app_main.c/ui_aufbauen) darf DIESER Bildschirm senkrecht scrollen: der
     * Inhalt ist ueber die Zeit gewachsen und passt nicht mehr auf 480 Pixel.
     * Per Screenshot belegt: der Signalbalken war halb abgeschnitten, der
     * Hinweis auf die Weboberflaeche lag komplett ausserhalb des Bildes und
     * war damit unerreichbar. Nur LV_DIR_VER - seitwaerts soll sich nichts
     * bewegen. */
    lv_obj_set_scroll_dir(s_einstellungen_screen, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_einstellungen_screen, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(s_einstellungen_screen, lv_color_hex(0x2196f3), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_einstellungen_screen, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(s_einstellungen_screen, 14, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s_einstellungen_screen, 7, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(s_einstellungen_screen, 4, LV_PART_SCROLLBAR);

    lv_obj_t *titel = lv_label_create(s_einstellungen_screen);
    lv_label_set_text(titel, text(TXT_EINSTELLUNGEN));
    lv_obj_set_style_text_font(titel, &schrift_mittel_40, 0);
    lv_obj_set_style_text_color(titel, lv_color_white(), 0);
    lv_obj_align(titel, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *btn_schliessen = einstellungen_nav_button_erzeugen(s_einstellungen_screen, text(TXT_SCHLIESSEN),
                                                                  einstellungen_schliessen_cb);
    lv_obj_align(btn_schliessen, LV_ALIGN_TOP_RIGHT, -20, 6);

    /* Reihe aus Buttons zu den Unterbildschirmen - lv_flex mit Zeilenumbruch
     * statt fester x-Positionen, damit unterschiedlich lange Beschriftungen
     * (LV_SIZE_CONTENT) einander nie ueberlappen koennen, egal wie breit
     * sie tatsaechlich ausfallen. */
    lv_obj_t *reihe = lv_obj_create(s_einstellungen_screen);
    lv_obj_remove_style_all(reihe);
    lv_obj_remove_flag(reihe, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(reihe, 740, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(reihe, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(reihe, 16, 0);
    lv_obj_set_style_pad_row(reihe, 12, 0);
    lv_obj_align(reihe, LV_ALIGN_TOP_LEFT, 30, 64);

    einstellungen_nav_button_erzeugen(reihe, text(TXT_WLAN_WECHSELN), einstellungen_wlan_cb);
    einstellungen_nav_button_erzeugen(reihe, text(TXT_DATUM_UHRZEIT), einstellungen_datum_cb);
    einstellungen_nav_button_erzeugen(reihe, text(TXT_KALENDER_ADRESSE), einstellungen_kalenderurl_cb);
    einstellungen_nav_button_erzeugen(reihe, text(TXT_DEMO_MODUS), einstellungen_demo_cb);

    /* Sprachknopf: zeigt Ausgangs- UND Zielsprache im selben Text ("Sprache:
     * Deutsch"), ein Tipp schaltet zur naechsten Sprache weiter (Wrap-Around
     * bei mehr als zwei). Der Name der Sprache steht IMMER in der Sprache
     * selbst (sprache_name), nicht in der gerade eingestellten - sonst
     * faende sich jemand, der die aktuelle Sprache nicht lesen kann, nie
     * zu seiner eigenen zurueck. */
    char sprache_beschriftung[48];
    snprintf(sprache_beschriftung, sizeof sprache_beschriftung, "%s: %s",
             text(TXT_SPRACHE), sprache_name(sprache_aktuell()));
    einstellungen_nav_button_erzeugen(reihe, sprache_beschriftung, einstellungen_sprache_cb);

    /* Tatsaechliche Hoehe der Reihe erst nach dem Layout-Durchlauf bekannt
     * (haengt davon ab, ob die Buttons in eine oder zwei Zeilen passen) -
     * siehe FALLSTRICKE_UND_WORKAROUNDS.md #11. */
    lv_obj_update_layout(reihe);
    int32_t naechste_y = 64 + lv_obj_get_height(reihe) + 14;

    /* Der Update-Bereich fuellt sich nach, sobald die Pruefung geantwortet
     * hat - deshalb eigener Container mit FESTER Hoehe. Waechst er nach dem
     * Aufbau, wuerde sonst alles darunter (Schalter, Signalbalken, Hinweis)
     * verrutschen; diese Elemente stehen an fest berechneten Y-Positionen. */
    s_update_bereich = lv_obj_create(s_einstellungen_screen);
    lv_obj_remove_style_all(s_update_bereich);
    lv_obj_remove_flag(s_update_bereich, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_update_bereich, 740, UPDATE_BEREICH_HOEHE);
    lv_obj_align(s_update_bereich, LV_ALIGN_TOP_LEFT, 30, naechste_y);
    /* Momentaufnahme entwerten: der Bereich ist gerade frisch und leer, jeder
     * Vergleich gegen den Stand des VORIGEN Menue-Aufbaus waere sinnlos. */
    s_update_stand_anzahl = -1;
    update_bereich_aufbauen();

    /* Sofort-Pruefung anstossen (telefoniert - darf nie hier im LVGL-Kontext
     * passieren, deshalb nur ein Anstoss an den Hintergrund-Task). Ohne das
     * zeigte das Menue nur den Stand der letzten Hintergrund-Pruefung, und
     * die laeuft erst 60 s nach dem Boot - wer ueber das Zahnrad des
     * Startbildschirms hereinkommt, war also fast immer zu frueh dran.
     * Steht noch kein WLAN (der Regelfall waehrend der WLAN-Boot-Phase),
     * holt update_bereich_pruefen() den Anstoss nach, sobald es steht. */
    if (netz_ist_verbunden())
        ota_pruefung_anstossen();
    else
        s_update_anstoss_offen = true;

    int32_t schalter_y = naechste_y + UPDATE_BEREICH_HOEHE + 14;

    einstellungen_schalter_zeile(s_einstellungen_screen, schalter_y, text(TXT_SIGNALTON),
                                  einstellungen_buzzer_aktiv(), einstellungen_buzzer_cb);

    /* WLAN-Signalstaerke als Text + Balken, regelmaessig aktualisiert
     * (signal_tick_cb) - fuer eine Vor-Ort-Diagnose, ob eine schwache
     * Verbindung an der aktuellen Position die Ursache fuer
     * Verbindungsabbrueche ist, ohne dass dafuer ein serieller Monitor
     * noetig waere. */
    s_signal_label = lv_label_create(s_einstellungen_screen);
    lv_obj_set_style_text_font(s_signal_label, &schrift_klein_28, 0);
    lv_obj_set_style_text_color(s_signal_label, lv_color_white(), 0);
    lv_obj_align(s_signal_label, LV_ALIGN_TOP_LEFT, 30, schalter_y + 48);

    s_signal_bar = lv_bar_create(s_einstellungen_screen);
    lv_bar_set_range(s_signal_bar, 0, 100);
    lv_obj_set_size(s_signal_bar, 300, 24);
    lv_obj_align(s_signal_bar, LV_ALIGN_TOP_LEFT, 30, schalter_y + 88);

    signal_aktualisieren();

    /* Weboberflaeche (webkonfig.c): Knopf schaltet Webserver+mDNS EIN/AUS
     * statt sie dauerhaft laufen zu lassen (FALLSTRICKE #39 - kosteten
     * zusammen ca. 17 KB internen SRAM, genug um jede Netzoperation
     * lahmzulegen). Startet nie von selbst; beim Verlassen des Menues wird
     * sicherheitshalber wieder ausgeschaltet (einrichtung_einstellungen_aufraeumen). */
    lv_obj_t *webkonfig_knopf = einstellungen_nav_button_erzeugen(
        s_einstellungen_screen, text(TXT_WEB_EINSCHALTEN), webkonfig_knopf_cb);
    lv_obj_align(webkonfig_knopf, LV_ALIGN_TOP_LEFT, 30, schalter_y + 124);
    s_webkonfig_knopf_label = lv_obj_get_child(webkonfig_knopf, 0);

    s_webkonfig_hinweis = lv_label_create(s_einstellungen_screen);
    lv_label_set_long_mode(s_webkonfig_hinweis, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_webkonfig_hinweis, 740);
    lv_obj_set_style_text_font(s_webkonfig_hinweis, &schrift_klein_28, 0);
    lv_obj_set_style_text_color(s_webkonfig_hinweis, lv_color_hex(0xa0a0a0), 0);
    s_webkonfig_hinweis_y = schalter_y + 180;
    lv_obj_align(s_webkonfig_hinweis, LV_ALIGN_TOP_LEFT, 30, s_webkonfig_hinweis_y);

    char firmware_text[64];
    snprintf(firmware_text, sizeof firmware_text, text(TXT_LAUFENDE_FIRMWARE), ota_laufende_version());
    s_firmware_label = lv_label_create(s_einstellungen_screen);
    lv_label_set_text(s_firmware_label, firmware_text);
    lv_obj_set_style_text_font(s_firmware_label, &schrift_klein_28, 0);
    lv_obj_set_style_text_color(s_firmware_label, lv_color_hex(0xa0a0a0), 0);

    /* Text setzen UND Firmware-Zeile positionieren - erst jetzt, wo beide
     * Labels existieren (siehe webkonfig_bereich_aktualisieren: die
     * tatsaechliche Hoehe des Hinweistexts wird gemessen statt geraten, dessen
     * Zeilenzahl je nach Zustand/IP-Adresse schwankt - FALLSTRICKE #22). */
    webkonfig_bereich_aktualisieren();

    /* Screenshot-Werkzeug: gleiches An/Aus-Knopf-Muster wie die Weboberflaeche
     * oben, unter der Firmware-Zeile. Position dynamisch gemessen statt
     * geraten (FALLSTRICKE #22) - die Hoehe von s_firmware_label steht erst
     * nach lv_obj_update_layout() fest. */
    lv_obj_update_layout(s_firmware_label);
    int32_t screenshot_y = lv_obj_get_y(s_firmware_label) + lv_obj_get_height(s_firmware_label) + 20;

    lv_obj_t *screenshot_knopf = einstellungen_nav_button_erzeugen(
        s_einstellungen_screen,
        screenshot_debug_laeuft() ? text(TXT_SCREENSHOT_AUSSCHALTEN) : text(TXT_SCREENSHOT_EINSCHALTEN),
        screenshot_knopf_cb);
    lv_obj_align(screenshot_knopf, LV_ALIGN_TOP_LEFT, 30, screenshot_y);
    s_screenshot_knopf_label = lv_obj_get_child(screenshot_knopf, 0);

    lv_obj_t *screenshot_hinweis = lv_label_create(s_einstellungen_screen);
    lv_label_set_long_mode(screenshot_hinweis, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(screenshot_hinweis, 740);
    lv_obj_set_style_text_font(screenshot_hinweis, &schrift_klein_28, 0);
    lv_obj_set_style_text_color(screenshot_hinweis, lv_color_hex(0xa0a0a0), 0);
    lv_label_set_text(screenshot_hinweis, text(TXT_SCREENSHOT_HINWEIS));
    lv_obj_align(screenshot_hinweis, LV_ALIGN_TOP_LEFT, 30, screenshot_y + 62);

    /* Timer nur EINMAL erzeugen und danach pausieren/fortsetzen statt bei
     * jedem Menue-Aufbau loeschen und neu anlegen: das staendige
     * lv_timer_delete/lv_timer_create bei den Bildschirm-Uebergaengen war
     * an dem in FALLSTRICKE #16 beschriebenen Endlos-Haenger der
     * LVGL-Task beteiligt (jede Timer-Aenderung startet die Runde in
     * lv_timer_handler von vorn). Der Callback greift ueber die statischen
     * Zeiger oben immer auf die Widgets des NEUESTEN Menue-Screens zu. */
    if (s_signal_timer == NULL)
        s_signal_timer = lv_timer_create(signal_tick_cb, 500, NULL);
    else
        lv_timer_resume(s_signal_timer);

    lv_screen_load(s_einstellungen_screen);
    lvgl_port_unlock();
}

einrichtung_status_t einrichtung_einstellungen_status(void)
{
    return s_einstellungen_status;
}

einstellungen_aktion_t einrichtung_einstellungen_aktion_abfragen(void)
{
    einstellungen_aktion_t aktion = s_einstellungen_aktion;
    s_einstellungen_aktion = EINSTELLUNGEN_AKTION_KEINE;
    return aktion;
}

void einrichtung_einstellungen_aufraeumen(void)
{
    /* Sicherheitsnetz: die Weboberflaeche soll den Menue-Bildschirm nie
     * ueberleben, egal auf welchem Weg er verlassen wird - sonst liefe sie
     * unbemerkt weiter und der Speicher bliebe knapp (FALLSTRICKE #39). */
    webkonfig_stop();

    lvgl_port_lock(0);
    if (s_signal_timer)
        lv_timer_pause(s_signal_timer); /* pausieren statt loeschen, siehe _zeigen */
    if (s_einstellungen_screen) {
        lv_obj_delete(s_einstellungen_screen);
        s_einstellungen_screen = NULL;
    }
    lvgl_port_unlock();
}

/* -------------------------------------------------------------------- */
/* Kalender-Adresse aendern                                              */
/* -------------------------------------------------------------------- */

static lv_obj_t *s_kalenderurl_screen;
static lv_obj_t *s_kalenderurl_ta;
static volatile einrichtung_status_t s_kalenderurl_status = EINRICHTUNG_OFFEN;

static void kalenderurl_speichern_cb(lv_event_t *e)
{
    (void)e;
    einstellungen_kalender_url_setzen(lv_textarea_get_text(s_kalenderurl_ta));
    s_kalenderurl_status = EINRICHTUNG_UEBERNOMMEN;
}

static void kalenderurl_abbrechen_cb(lv_event_t *e)
{
    (void)e;
    s_kalenderurl_status = EINRICHTUNG_ABGEBROCHEN;
}

void einrichtung_kalenderurl_zeigen(void)
{
    lvgl_port_lock(0);
    s_kalenderurl_status = EINRICHTUNG_OFFEN;

    s_kalenderurl_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_kalenderurl_screen, lv_color_black(), 0);
    lv_obj_remove_flag(s_kalenderurl_screen, LV_OBJ_FLAG_SCROLLABLE); /* siehe app_main.c/ui_aufbauen */

    lv_obj_t *titel = lv_label_create(s_kalenderurl_screen);
    lv_label_set_text(titel, text(TXT_KALENDER_ADRESSE));
    lv_obj_set_style_text_font(titel, &schrift_mittel_40, 0);
    lv_obj_set_style_text_color(titel, lv_color_white(), 0);
    lv_obj_align(titel, LV_ALIGN_TOP_MID, 0, 15);

    /* Mehrzeilig (kein one_line) und ueber fast die volle Breite - die
     * bisherige einzeilige Variante im Einstellungen-Menue lief bei einer
     * langen ICS-URL seitlich aus dem Textfeld heraus und war nicht mehr
     * vollstaendig lesbar. */
    s_kalenderurl_ta = lv_textarea_create(s_kalenderurl_screen);
    lv_textarea_set_one_line(s_kalenderurl_ta, false);
    lv_textarea_set_placeholder_text(s_kalenderurl_ta, text(TXT_KALENDER_URL_PLATZHALTER));
    char aktuelle_url[EINSTELLUNGEN_KALENDER_URL_MAX];
    einstellungen_kalender_url_effektiv(aktuelle_url, sizeof aktuelle_url);
    lv_textarea_set_text(s_kalenderurl_ta, aktuelle_url);
    lv_obj_set_style_text_font(s_kalenderurl_ta, &schrift_klein_28, 0);
    lv_obj_set_size(s_kalenderurl_ta, 740, 110);
    lv_obj_align(s_kalenderurl_ta, LV_ALIGN_TOP_MID, 0, 65);

    lv_obj_t *btn_speichern = lv_button_create(s_kalenderurl_screen);
    lv_obj_set_size(btn_speichern, 260, 50);
    lv_obj_align(btn_speichern, LV_ALIGN_TOP_LEFT, 30, 185);
    lv_obj_add_event_cb(btn_speichern, kalenderurl_speichern_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l1 = lv_label_create(btn_speichern);
    lv_label_set_text(l1, text(TXT_SPEICHERN));
    lv_obj_set_style_text_font(l1, &schrift_klein_28, 0);
    lv_obj_center(l1);

    lv_obj_t *btn_abbrechen = lv_button_create(s_kalenderurl_screen);
    lv_obj_set_size(btn_abbrechen, 200, 50);
    lv_obj_align(btn_abbrechen, LV_ALIGN_TOP_RIGHT, -30, 185);
    lv_obj_add_event_cb(btn_abbrechen, kalenderurl_abbrechen_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l2 = lv_label_create(btn_abbrechen);
    lv_label_set_text(l2, text(TXT_ABBRECHEN));
    lv_obj_set_style_text_font(l2, &schrift_klein_28, 0);
    lv_obj_center(l2);

    lv_obj_t *keyboard = lv_keyboard_create(s_kalenderurl_screen);
    lv_obj_set_size(keyboard, LV_PCT(100), 235);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(keyboard, s_kalenderurl_ta);

    lv_screen_load(s_kalenderurl_screen);
    lvgl_port_unlock();
}

einrichtung_status_t einrichtung_kalenderurl_status(void)
{
    return s_kalenderurl_status;
}

void einrichtung_kalenderurl_aufraeumen(void)
{
    lvgl_port_lock(0);
    if (s_kalenderurl_screen) {
        lv_obj_delete(s_kalenderurl_screen);
        s_kalenderurl_screen = NULL;
    }
    lvgl_port_unlock();
}
