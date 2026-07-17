#include "einrichtung.h"
#include "einstellungen.h"
#include "netz.h"
#include "zeit.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_lvgl_port.h"

LV_FONT_DECLARE(schrift_mittel_40);
LV_FONT_DECLARE(schrift_klein_28);

/* -------------------------------------------------------------------- */
/* WLAN-Zugangsdaten                                                     */
/* -------------------------------------------------------------------- */

static lv_obj_t *s_wlan_screen;
static lv_obj_t *s_ssid_ta;
static lv_obj_t *s_pass_ta;
static lv_obj_t *s_wlan_keyboard;
static volatile einrichtung_status_t s_wlan_status = EINRICHTUNG_OFFEN;

static void wlan_textarea_fokus_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    lv_keyboard_set_textarea(s_wlan_keyboard, ta);
}

static void wlan_speichern_cb(lv_event_t *e)
{
    (void)e;
    const char *ssid = lv_textarea_get_text(s_ssid_ta);
    if (strlen(ssid) == 0)
        return; /* ohne Netzwerkname nichts zu speichern */
    /* Bei Erfolg startet das Geraet hier neu und kehrt nicht zurueck. */
    netz_zugangsdaten_speichern(ssid, lv_textarea_get_text(s_pass_ta));
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
    lv_label_set_text(titel, "WLAN-Zugangsdaten aendern");
    lv_obj_set_style_text_font(titel, &schrift_mittel_40, 0);
    lv_obj_set_style_text_color(titel, lv_color_white(), 0);
    lv_obj_align(titel, LV_ALIGN_TOP_MID, 0, 15);

    s_ssid_ta = lv_textarea_create(s_wlan_screen);
    lv_textarea_set_one_line(s_ssid_ta, true);
    lv_textarea_set_placeholder_text(s_ssid_ta, "Netzwerkname (SSID)");
    lv_obj_set_style_text_font(s_ssid_ta, &schrift_klein_28, 0);
    lv_obj_set_size(s_ssid_ta, 600, 50);
    lv_obj_align(s_ssid_ta, LV_ALIGN_TOP_MID, 0, 65);
    lv_obj_add_event_cb(s_ssid_ta, wlan_textarea_fokus_cb, LV_EVENT_FOCUSED, NULL);

    s_pass_ta = lv_textarea_create(s_wlan_screen);
    lv_textarea_set_one_line(s_pass_ta, true);
    lv_textarea_set_password_mode(s_pass_ta, true);
    lv_textarea_set_placeholder_text(s_pass_ta, "Passwort");
    lv_obj_set_style_text_font(s_pass_ta, &schrift_klein_28, 0);
    lv_obj_set_size(s_pass_ta, 600, 50);
    lv_obj_align(s_pass_ta, LV_ALIGN_TOP_MID, 0, 122);
    lv_obj_add_event_cb(s_pass_ta, wlan_textarea_fokus_cb, LV_EVENT_FOCUSED, NULL);

    lv_obj_t *btn_speichern = lv_button_create(s_wlan_screen);
    lv_obj_set_size(btn_speichern, 260, 55);
    lv_obj_align(btn_speichern, LV_ALIGN_TOP_LEFT, 30, 180);
    lv_obj_add_event_cb(btn_speichern, wlan_speichern_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l1 = lv_label_create(btn_speichern);
    lv_label_set_text(l1, "Speichern");
    lv_obj_set_style_text_font(l1, &schrift_klein_28, 0);
    lv_obj_center(l1);

    lv_obj_t *btn_abbrechen = lv_button_create(s_wlan_screen);
    lv_obj_set_size(btn_abbrechen, 200, 55);
    lv_obj_align(btn_abbrechen, LV_ALIGN_TOP_RIGHT, -30, 180);
    lv_obj_add_event_cb(btn_abbrechen, wlan_abbrechen_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l2 = lv_label_create(btn_abbrechen);
    lv_label_set_text(l2, "Abbrechen");
    lv_obj_set_style_text_font(l2, &schrift_klein_28, 0);
    lv_obj_center(l2);

    /* Tastaturhoehe so gewaehlt, dass ihre Oberkante (480-235=245) unter den
     * Buttons endet (180+55=235) - sonst ueberlappen sich Buttons und
     * Tastatur im unteren Bildschirmdrittel. */
    s_wlan_keyboard = lv_keyboard_create(s_wlan_screen);
    lv_obj_set_size(s_wlan_keyboard, LV_PCT(100), 235);
    lv_obj_align(s_wlan_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(s_wlan_keyboard, s_ssid_ta);

    lv_screen_load(s_wlan_screen);
    lvgl_port_unlock();
}

einrichtung_status_t einrichtung_wlan_status(void)
{
    return s_wlan_status;
}

void einrichtung_wlan_aufraeumen(void)
{
    lvgl_port_lock(0);
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
    lv_label_set_text(titel, "Datum und Uhrzeit einstellen");
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
    static const char *monat_optionen =
        "Januar\nFebruar\nMaerz\nApril\nMai\nJuni\n"
        "Juli\nAugust\nSeptember\nOktober\nNovember\nDezember";

    /* Spaltenmittelpunkte relativ zur Bildschirmmitte, je nach Breite des
     * Inhalts (der Monatsname "September" braucht deutlich mehr Platz als
     * eine zweistellige Zahl). */
    s_roller_tag = roller_erzeugen(s_zeit_screen, "Tag", tag_optionen, -310, 100, start_tag);
    s_roller_monat = roller_erzeugen(s_zeit_screen, "Monat", monat_optionen, -130, 220, start_monat);
    s_roller_jahr = roller_erzeugen(s_zeit_screen, "Jahr", jahr_optionen, 60, 120, start_jahr);
    s_roller_stunde = roller_erzeugen(s_zeit_screen, "Std", stunde_optionen, 190, 100, start_stunde);
    s_roller_minute = roller_erzeugen(s_zeit_screen, "Min", minute_optionen, 310, 100, start_minute);

    lv_obj_t *btn_uebernehmen = lv_button_create(s_zeit_screen);
    lv_obj_set_size(btn_uebernehmen, 260, 60);
    lv_obj_align(btn_uebernehmen, LV_ALIGN_BOTTOM_MID, -150, -20);
    lv_obj_add_event_cb(btn_uebernehmen, zeit_uebernehmen_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l1 = lv_label_create(btn_uebernehmen);
    lv_label_set_text(l1, "Uebernehmen");
    lv_obj_set_style_text_font(l1, &schrift_klein_28, 0);
    lv_obj_center(l1);

    lv_obj_t *btn_abbrechen = lv_button_create(s_zeit_screen);
    lv_obj_set_size(btn_abbrechen, 200, 60);
    lv_obj_align(btn_abbrechen, LV_ALIGN_BOTTOM_MID, 150, -20);
    lv_obj_add_event_cb(btn_abbrechen, zeit_abbrechen_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l2 = lv_label_create(btn_abbrechen);
    lv_label_set_text(l2, "Abbrechen");
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

    s_einstellungen_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_einstellungen_screen, lv_color_black(), 0);
    lv_obj_remove_flag(s_einstellungen_screen, LV_OBJ_FLAG_SCROLLABLE); /* siehe app_main.c/ui_aufbauen */

    lv_obj_t *titel = lv_label_create(s_einstellungen_screen);
    lv_label_set_text(titel, "Einstellungen");
    lv_obj_set_style_text_font(titel, &schrift_mittel_40, 0);
    lv_obj_set_style_text_color(titel, lv_color_white(), 0);
    lv_obj_align(titel, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *btn_schliessen = einstellungen_nav_button_erzeugen(s_einstellungen_screen, "Schliessen",
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

    einstellungen_nav_button_erzeugen(reihe, "WLAN wechseln", einstellungen_wlan_cb);
    einstellungen_nav_button_erzeugen(reihe, "Datum, Uhrzeit einstellen", einstellungen_datum_cb);
    einstellungen_nav_button_erzeugen(reihe, "Kalender-Adresse aendern", einstellungen_kalenderurl_cb);

    /* Tatsaechliche Hoehe der Reihe erst nach dem Layout-Durchlauf bekannt
     * (haengt davon ab, ob die drei Buttons in eine oder zwei Zeilen
     * passen) - siehe FALLSTRICKE_UND_WORKAROUNDS.md #11. */
    lv_obj_update_layout(reihe);
    int32_t schalter_y = 64 + lv_obj_get_height(reihe) + 20;

    einstellungen_schalter_zeile(s_einstellungen_screen, schalter_y, "Signalton bei Erinnerungen",
                                  einstellungen_buzzer_aktiv(), einstellungen_buzzer_cb);

    /* WLAN-Signalstaerke als reine Info-Zeile (kein Schalter/Button) - fuer
     * eine Vor-Ort-Diagnose, ob eine schwache Verbindung an der aktuellen
     * Position die Ursache fuer Verbindungsabbrueche ist, ohne dass dafuer
     * ein serieller Monitor noetig waere. Nur eine Momentaufnahme beim
     * Oeffnen dieses Bildschirms, kein Live-Update. */
    int rssi = netz_rssi_dbm();
    char signal_text[64];
    if (rssi == 0) {
        snprintf(signal_text, sizeof signal_text, "WLAN-Signal: nicht verbunden");
    } else {
        const char *guete = rssi >= -67 ? "gut" : (rssi >= -80 ? "schwach" : "sehr schwach");
        snprintf(signal_text, sizeof signal_text, "WLAN-Signal: %d dBm (%s)", rssi, guete);
    }
    lv_obj_t *signal_label = lv_label_create(s_einstellungen_screen);
    lv_label_set_text(signal_label, signal_text);
    lv_obj_set_style_text_font(signal_label, &schrift_klein_28, 0);
    lv_obj_set_style_text_color(signal_label, lv_color_white(), 0);
    lv_obj_align(signal_label, LV_ALIGN_TOP_LEFT, 30, schalter_y + 48);

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
    lvgl_port_lock(0);
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
    lv_label_set_text(titel, "Kalender-Adresse aendern");
    lv_obj_set_style_text_font(titel, &schrift_mittel_40, 0);
    lv_obj_set_style_text_color(titel, lv_color_white(), 0);
    lv_obj_align(titel, LV_ALIGN_TOP_MID, 0, 15);

    /* Mehrzeilig (kein one_line) und ueber fast die volle Breite - die
     * bisherige einzeilige Variante im Einstellungen-Menue lief bei einer
     * langen ICS-URL seitlich aus dem Textfeld heraus und war nicht mehr
     * vollstaendig lesbar. */
    s_kalenderurl_ta = lv_textarea_create(s_kalenderurl_screen);
    lv_textarea_set_one_line(s_kalenderurl_ta, false);
    lv_textarea_set_placeholder_text(s_kalenderurl_ta, "Kalender-Adresse (ICS-URL)");
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
    lv_label_set_text(l1, "Speichern");
    lv_obj_set_style_text_font(l1, &schrift_klein_28, 0);
    lv_obj_center(l1);

    lv_obj_t *btn_abbrechen = lv_button_create(s_kalenderurl_screen);
    lv_obj_set_size(btn_abbrechen, 200, 50);
    lv_obj_align(btn_abbrechen, LV_ALIGN_TOP_RIGHT, -30, 185);
    lv_obj_add_event_cb(btn_abbrechen, kalenderurl_abbrechen_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l2 = lv_label_create(btn_abbrechen);
    lv_label_set_text(l2, "Abbrechen");
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
