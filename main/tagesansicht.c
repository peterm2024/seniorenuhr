#include "tagesansicht.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_lvgl_port.h"
#include "kalender_anzeige.h"
#include "zeit.h"

LV_FONT_DECLARE(schrift_klein_28);
LV_FONT_DECLARE(schrift_mittel_40);

#define TAGE_ANZAHL 7      /* gestern, heute, morgen, +2..+5 */
#define HEUTE_INDEX 1      /* Position von "heute" in der 7er-Spalte */

#define BUTTON_BREITE 56
#define BUTTON_HOEHE  62
#define BUTTON_GAP    4
#define SPALTE_X      6
#define SPALTE_Y      8    /* nutzt die Bildschirmhoehe fast vollstaendig aus */
#define SPALTE_HOEHE  (TAGE_ANZAHL * BUTTON_HOEHE + (TAGE_ANZAHL - 1) * BUTTON_GAP)

#define HEUTE_BTN_BREITE 70
#define BILDSCHIRM_BREITE 800

#define FARBE_BUTTON_HINTERGRUND 0xd8d8d8 /* hellgrau statt schwarz/transparent */
#define FARBE_BUTTON_TEXT        0x1a1a2e /* dunkel, gut lesbar auf hellgrau */

#define FENSTER_BREITE       620
#define FENSTER_HOEHE_TAG    300
#define FENSTER_HOEHE_HEUTE  400
#define FARBE_FENSTER_TEXT   0xffffff
#define FARBE_FENSTER_AKZENT 0xffd75f

#define TAGESFENSTER_ANZEIGEDAUER_MS (15 * 1000)
#define HEUTEFENSTER_INAKTIV_MS      (5 * 60 * 1000)

/* Titel (ICS_TITEL_MAX) plus Platz fuer "HH:MM  "-Prefix. */
#define ZEILE_MAX (ICS_TITEL_MAX + 16)

static lv_obj_t *s_scr;
static lv_obj_t *s_tag_buttons[TAGE_ANZAHL]; /* NULL bei HEUTE_INDEX (kein Button) */
static lv_obj_t *s_tag_labels[TAGE_ANZAHL];
static lv_obj_t *s_heute_button;
static int s_letzter_tag_schluessel = -1;

static lv_obj_t *s_tages_fenster;
static lv_timer_t *s_tages_fenster_timer;
static lv_obj_t *s_heute_fenster;
static lv_timer_t *s_heute_fenster_timer;

static void tagesfenster_intern_schliessen(void)
{
    if (s_tages_fenster_timer) {
        lv_timer_delete(s_tages_fenster_timer);
        s_tages_fenster_timer = NULL;
    }
    if (s_tages_fenster) {
        lv_obj_delete(s_tages_fenster);
        s_tages_fenster = NULL;
    }
}

static void heutefenster_intern_schliessen(void)
{
    if (s_heute_fenster_timer) {
        lv_timer_delete(s_heute_fenster_timer);
        s_heute_fenster_timer = NULL;
    }
    if (s_heute_fenster) {
        lv_obj_delete(s_heute_fenster);
        s_heute_fenster = NULL;
    }
}

static void tagesfenster_timer_cb(lv_timer_t *t)
{
    (void)t;
    lvgl_port_lock(0);
    tagesfenster_intern_schliessen();
    lvgl_port_unlock();
}

static void heutefenster_timer_cb(lv_timer_t *t)
{
    (void)t;
    lvgl_port_lock(0);
    heutefenster_intern_schliessen();
    lvgl_port_unlock();
}

/* Manuelles Schliessen per "X"-Button - beide Fenster boten bisher nur
 * das automatische Timeout an, das wurde als fehlende Bedienmoeglichkeit
 * zurueckgemeldet. */
static void tagesfenster_schliessen_cb(lv_event_t *e)
{
    (void)e;
    lvgl_port_lock(0);
    tagesfenster_intern_schliessen();
    lvgl_port_unlock();
}

static void heutefenster_schliessen_cb(lv_event_t *e)
{
    (void)e;
    lvgl_port_lock(0);
    heutefenster_intern_schliessen();
    lvgl_port_unlock();
}

/* Baut das gemeinsame Grundgeruest (dunkles Panel + Ueberschrift + "X"-
 * Schliessen-Button) fuer beide Fenstertypen. Muss innerhalb eines bereits
 * gehaltenen LVGL-Locks aufgerufen werden (siehe Aufrufer unten). */
static lv_obj_t *fenster_grundgeruest_erzeugen(const char *titel, int32_t hoehe, lv_event_cb_t schliessen_cb)
{
    lv_obj_t *panel = lv_obj_create(s_scr);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(panel, FENSTER_BREITE, hoehe);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_90, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, lv_color_white(), 0);
    lv_obj_set_style_radius(panel, 12, 0);

    lv_obj_t *kopf = lv_label_create(panel);
    lv_label_set_text(kopf, titel);
    lv_obj_set_style_text_font(kopf, &schrift_mittel_40, 0);
    lv_obj_set_style_text_color(kopf, lv_color_hex(FARBE_FENSTER_TEXT), 0);
    lv_obj_align(kopf, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *btn_schliessen = lv_button_create(panel);
    lv_obj_set_size(btn_schliessen, 46, 46);
    lv_obj_set_style_bg_color(btn_schliessen, lv_color_hex(FARBE_BUTTON_HINTERGRUND), 0);
    lv_obj_set_style_bg_opa(btn_schliessen, LV_OPA_COVER, 0);
    lv_obj_align(btn_schliessen, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_add_event_cb(btn_schliessen, schliessen_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *x_label = lv_label_create(btn_schliessen);
    lv_label_set_text(x_label, "X");
    lv_obj_set_style_text_font(x_label, &schrift_klein_28, 0);
    lv_obj_set_style_text_color(x_label, lv_color_hex(FARBE_BUTTON_TEXT), 0);
    lv_obj_center(x_label);

    return panel;
}

static void eintrag_zeile_formatieren(const kalender_tag_eintrag_t *e, char *ziel, size_t ziel_groesse)
{
    /* Explizite Praezision (dynamisch aus der Zielgroesse) statt nacktem
     * "%s": ueber einen Zeiger zugegriffene Array-Felder (e->titel)
     * verlieren bei GCCs Format-Truncation-Pruefung ihre bekannte Groesse,
     * wodurch ein sehr grosses Worst-Case-Ergebnis angenommen und
     * -Werror=format-truncation ausgeloest wird. */
    if (e->ganztags)
        snprintf(ziel, ziel_groesse, "%.*s", (int)ziel_groesse - 1, e->titel);
    else
        snprintf(ziel, ziel_groesse, "%02d:%02d  %.*s", e->stunde, e->minute, (int)ziel_groesse - 8, e->titel);
}

/* ---- Tages-Fenster (read-only, 15s) --------------------------------- */

static void tages_fenster_oeffnen(int tage_versatz)
{
    kalender_tag_eintrag_t eintraege[KALENDER_EINTRAEGE_MAX];
    int anzahl = kalender_anzeige_eintraege_fuer_tag(tage_versatz, eintraege, KALENDER_EINTRAEGE_MAX);

    time_t tag_zeit = time(NULL) + (time_t)tage_versatz * 86400;
    struct tm lokal;
    localtime_r(&tag_zeit, &lokal);
    char datum[32];
    zeit_datum_text(&lokal, datum, sizeof datum);
    char titel[56];
    snprintf(titel, sizeof titel, "%s, %s", zeit_wochentag_gross(&lokal), datum);

    char text[512];
    size_t belegt = 0;
    text[0] = '\0';
    if (anzahl == 0) {
        snprintf(text, sizeof text, "Keine Eintraege.");
    } else {
        for (int i = 0; i < anzahl; i++) {
            char zeile[ZEILE_MAX + 16];
            char inhalt[ZEILE_MAX];
            eintrag_zeile_formatieren(&eintraege[i], inhalt, sizeof inhalt);
            snprintf(zeile, sizeof zeile, "%s%s\n", eintraege[i].ist_tablette ? "Tablette: " : "", inhalt);
            size_t n = strlen(zeile);
            if (belegt + n < sizeof text) {
                memcpy(text + belegt, zeile, n);
                belegt += n;
                text[belegt] = '\0';
            }
        }
    }

    lvgl_port_lock(0);
    tagesfenster_intern_schliessen();
    heutefenster_intern_schliessen(); /* nur ein Fenster gleichzeitig */

    s_tages_fenster = fenster_grundgeruest_erzeugen(titel, FENSTER_HOEHE_TAG, tagesfenster_schliessen_cb);

    lv_obj_t *liste = lv_label_create(s_tages_fenster);
    lv_obj_set_style_text_font(liste, &schrift_klein_28, 0);
    lv_obj_set_style_text_color(liste, lv_color_hex(FARBE_FENSTER_TEXT), 0);
    lv_obj_set_width(liste, FENSTER_BREITE - 40);
    lv_label_set_long_mode(liste, LV_LABEL_LONG_WRAP);
    lv_obj_align(liste, LV_ALIGN_TOP_LEFT, 20, 70);
    lv_label_set_text(liste, text);

    s_tages_fenster_timer = lv_timer_create(tagesfenster_timer_cb, TAGESFENSTER_ANZEIGEDAUER_MS, NULL);
    lv_timer_set_repeat_count(s_tages_fenster_timer, 1);
    lvgl_port_unlock();
}

static void tag_button_cb(lv_event_t *e)
{
    int versatz = (int)(intptr_t)lv_event_get_user_data(e);
    tages_fenster_oeffnen(versatz);
}

/* ---- Heute-Fenster (mit Bestaetigungs-Schaltern, 5min Inaktivitaet) - */

static void heute_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    bool an = lv_obj_has_state(sw, LV_STATE_CHECKED);
    kalender_anzeige_tablette_bestaetigen(index, an);

    lvgl_port_lock(0);
    if (s_heute_fenster_timer)
        lv_timer_reset(s_heute_fenster_timer);
    lvgl_port_unlock();
}

static void heutefenster_hintergrund_cb(lv_event_t *e)
{
    (void)e;
    lvgl_port_lock(0);
    if (s_heute_fenster_timer)
        lv_timer_reset(s_heute_fenster_timer);
    lvgl_port_unlock();
}

static void heute_button_cb(lv_event_t *e)
{
    (void)e;

    kalender_tag_eintrag_t eintraege[KALENDER_EINTRAEGE_MAX];
    int anzahl = kalender_anzeige_heutige_eintraege(eintraege, KALENDER_EINTRAEGE_MAX);

    time_t jetzt = time(NULL);
    struct tm lokal;
    localtime_r(&jetzt, &lokal);
    char datum[32];
    zeit_datum_text(&lokal, datum, sizeof datum);
    char titel[56];
    snprintf(titel, sizeof titel, "HEUTE, %s", datum);

    lvgl_port_lock(0);
    heutefenster_intern_schliessen();
    tagesfenster_intern_schliessen(); /* nur ein Fenster gleichzeitig */

    s_heute_fenster = fenster_grundgeruest_erzeugen(titel, FENSTER_HOEHE_HEUTE, heutefenster_schliessen_cb);
    lv_obj_add_event_cb(s_heute_fenster, heutefenster_hintergrund_cb, LV_EVENT_PRESSED, NULL);

    int32_t y = 70;
    bool tablette_vorhanden = false;
    for (int i = 0; i < anzahl; i++) {
        if (!eintraege[i].ist_tablette)
            continue;
        tablette_vorhanden = true;

        char inhalt[ZEILE_MAX];
        eintrag_zeile_formatieren(&eintraege[i], inhalt, sizeof inhalt);

        lv_obj_t *label = lv_label_create(s_heute_fenster);
        lv_label_set_text(label, inhalt);
        lv_obj_set_style_text_font(label, &schrift_klein_28, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(FARBE_FENSTER_TEXT), 0);
        lv_obj_set_pos(label, 20, y + 8);

        lv_obj_t *sw = lv_switch_create(s_heute_fenster);
        lv_obj_set_pos(sw, FENSTER_BREITE - 100, y);
        if (eintraege[i].bestaetigt)
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, heute_switch_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);

        y += 55;
    }
    if (!tablette_vorhanden) {
        lv_obj_t *label = lv_label_create(s_heute_fenster);
        lv_label_set_text(label, "Keine Tabletten heute.");
        lv_obj_set_style_text_font(label, &schrift_klein_28, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(FARBE_FENSTER_TEXT), 0);
        lv_obj_set_pos(label, 20, y + 8);
        y += 45;
    }

    bool termin_ueberschrift_da = false;
    for (int i = 0; i < anzahl; i++) {
        if (eintraege[i].ist_tablette)
            continue;
        if (!termin_ueberschrift_da) {
            lv_obj_t *ueberschrift = lv_label_create(s_heute_fenster);
            lv_label_set_text(ueberschrift, "TERMINE");
            lv_obj_set_style_text_font(ueberschrift, &schrift_klein_28, 0);
            lv_obj_set_style_text_color(ueberschrift, lv_color_hex(FARBE_FENSTER_AKZENT), 0);
            lv_obj_set_pos(ueberschrift, 20, y + 8);
            y += 38;
            termin_ueberschrift_da = true;
        }
        char inhalt[ZEILE_MAX];
        eintrag_zeile_formatieren(&eintraege[i], inhalt, sizeof inhalt);
        lv_obj_t *label = lv_label_create(s_heute_fenster);
        lv_label_set_text(label, inhalt);
        lv_obj_set_style_text_font(label, &schrift_klein_28, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(FARBE_FENSTER_TEXT), 0);
        lv_obj_set_pos(label, 20, y + 8);
        y += 36;
    }

    s_heute_fenster_timer = lv_timer_create(heutefenster_timer_cb, HEUTEFENSTER_INAKTIV_MS, NULL);
    lvgl_port_unlock();
}

/* ---- Aufbau/Beschriftung der Buttons --------------------------------- */

static void button_grundstil(lv_obj_t *btn)
{
    lv_obj_remove_style_all(btn);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(btn, lv_color_hex(FARBE_BUTTON_HINTERGRUND), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 6, 0);
}

void tagesansicht_erstellen(lv_obj_t *scr)
{
    s_scr = scr;

    for (int i = 0; i < TAGE_ANZAHL; i++) {
        int32_t y = SPALTE_Y + i * (BUTTON_HOEHE + BUTTON_GAP);
        int versatz = i - HEUTE_INDEX;

        if (versatz == 0) {
            /* "Heute" ist bewusst kein Button - nur ein Pfeil zur
             * Orientierung, wo "heute" in der Spalte sitzt. Gleiche
             * Hintergrundfarbe wie die Buttons, damit die Spalte optisch
             * durchgehend wirkt. */
            lv_obj_t *platz = lv_label_create(scr);
            lv_label_set_text(platz, ">");
            lv_obj_set_style_text_font(platz, &schrift_klein_28, 0);
            lv_obj_set_style_text_color(platz, lv_color_hex(FARBE_BUTTON_TEXT), 0);
            lv_obj_set_style_text_align(platz, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_bg_color(platz, lv_color_hex(FARBE_BUTTON_HINTERGRUND), 0);
            lv_obj_set_style_bg_opa(platz, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(platz, 6, 0);
            lv_obj_set_size(platz, BUTTON_BREITE, BUTTON_HOEHE);
            lv_obj_set_pos(platz, SPALTE_X, y);
            lv_obj_remove_flag(platz, LV_OBJ_FLAG_CLICKABLE);
            s_tag_buttons[i] = NULL;
            s_tag_labels[i] = platz;
            continue;
        }

        lv_obj_t *btn = lv_button_create(scr);
        button_grundstil(btn);
        lv_obj_set_size(btn, BUTTON_BREITE, BUTTON_HOEHE);
        lv_obj_set_pos(btn, SPALTE_X, y);
        lv_obj_add_event_cb(btn, tag_button_cb, LV_EVENT_CLICKED, (void *)(intptr_t)versatz);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, "...");
        lv_obj_set_style_text_font(label, &schrift_klein_28, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(FARBE_BUTTON_TEXT), 0);
        lv_obj_center(label);

        s_tag_buttons[i] = btn;
        s_tag_labels[i] = label;
    }

    s_heute_button = lv_button_create(scr);
    button_grundstil(s_heute_button);
    lv_obj_set_size(s_heute_button, HEUTE_BTN_BREITE, SPALTE_HOEHE);
    lv_obj_set_pos(s_heute_button, BILDSCHIRM_BREITE - HEUTE_BTN_BREITE - SPALTE_X, SPALTE_Y);
    lv_obj_add_event_cb(s_heute_button, heute_button_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *heute_label = lv_label_create(s_heute_button);
    lv_label_set_text(heute_label, "H\nE\nU\nT\nE");
    lv_obj_set_style_text_font(heute_label, &schrift_klein_28, 0);
    lv_obj_set_style_text_color(heute_label, lv_color_hex(FARBE_BUTTON_TEXT), 0);
    lv_obj_set_style_text_align(heute_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(heute_label);
}

void tagesansicht_tag_aktualisieren(void)
{
    if (!zeit_ist_synchron())
        return;

    time_t jetzt = time(NULL);
    struct tm lokal;
    localtime_r(&jetzt, &lokal);
    int schluessel = (lokal.tm_year + 1900) * 10000 + (lokal.tm_mon + 1) * 100 + lokal.tm_mday;
    if (schluessel == s_letzter_tag_schluessel)
        return;
    s_letzter_tag_schluessel = schluessel;

    lvgl_port_lock(0);
    for (int i = 0; i < TAGE_ANZAHL; i++) {
        int versatz = i - HEUTE_INDEX;
        if (versatz == 0)
            continue; /* Pfeil-Platzhalter braucht keinen Text */
        time_t tag_zeit = jetzt + (time_t)versatz * 86400;
        struct tm tag_lokal;
        localtime_r(&tag_zeit, &tag_lokal);
        lv_label_set_text(s_tag_labels[i], zeit_wochentag_kurz(&tag_lokal));
    }
    lvgl_port_unlock();
}

void tagesansicht_sichtbarkeit_setzen(bool sichtbar)
{
    for (int i = 0; i < TAGE_ANZAHL; i++) {
        lv_obj_t *obj = s_tag_buttons[i] ? s_tag_buttons[i] : s_tag_labels[i];
        if (sichtbar)
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    if (sichtbar)
        lv_obj_remove_flag(s_heute_button, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(s_heute_button, LV_OBJ_FLAG_HIDDEN);
}
