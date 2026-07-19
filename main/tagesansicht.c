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
/* Waagerechtes Innenpolster des "Heute"-Buttons (Position 2) - dessen
 * Breite sich per LV_SIZE_CONTENT exakt an "Heute" anpasst, siehe
 * tagesansicht_erstellen. */
#define HEUTE_BTN_PAD 14
#define BUTTON_GAP    4
#define SPALTE_X      6
#define SPALTE_Y      8    /* nutzt die Bildschirmhoehe fast vollstaendig aus */
#define SPALTE_HOEHE  (TAGE_ANZAHL * BUTTON_HOEHE + (TAGE_ANZAHL - 1) * BUTTON_GAP)

/* Aus FARBE_TAG_HINTERGRUND (app_main.c, 0x123a63 = 18/58/99) errechnet:
 * jeder Farbkanal um 20% angehoben (22/70/119) - die Buttons sollen sich
 * nur dezent vom Hintergrund abheben, nicht wie zuvor (festes Hellgrau)
 * stark hervorstechen. */
#define FARBE_BUTTON_HINTERGRUND 0x164677
#define FARBE_BUTTON_TEXT        0xd0e0f0 /* helles Blau-Weiss, wie der uebrige Anzeigetext */
#define FARBE_BUTTON_RAND_AKTIV  0x6ec6ff /* hellblauer Rahmen: Button, dessen Fenster gerade offen ist */

/* Beschriftungsfarbe der Wochentag-Buttons je nach Anzahl Termine an dem
 * Tag - auf einen Blick erkennbar, ohne das Fenster oeffnen zu muessen. */
#define FARBE_TERMIN_1       0xffcc80 /* 1 Termin: helles Orange */
#define FARBE_TERMIN_2       0xff9800 /* 2 Termine: dunkles Orange */
#define FARBE_TERMIN_3PLUS   0xff5252 /* 3+ Termine: Rot */

#define FENSTER_BREITE       620
#define FENSTER_HOEHE_TAG    300
#define FENSTER_HOEHE_HEUTE  440
#define FARBE_FENSTER_TEXT   0xffffff
#define FARBE_FENSTER_AKZENT 0xffd75f
#define FARBE_VERGANGEN      0x707a8a /* gedaempftes Grau fuer bereits vergangene Termine */

/* Schiebeschalter statt lv_switch: bei Zittern/Ungenauigkeit im Alter
 * loest ein einfacher Tipp auf einen Schalter zu leicht eine Fehleingabe
 * aus. Ein langer Schieberweg (~5cm bei diesem Display) muss bewusst
 * durchgezogen werden - ein kurzer Tipp irgendwo auf der Spur bewegt den
 * Knopf nicht ueber die Schwelle und die Stellung bleibt unveraendert. */
#define SCHIEBER_BREITE        195 /* 25% kuerzer als urspruenglich (260) */
#define SCHIEBER_HOEHE         50
#define SCHIEBER_KNOPF         46
#define SCHIEBER_TRAVEL        (SCHIEBER_BREITE - SCHIEBER_KNOPF)
/* Abstand zum rechten Fensterrand - bewusst gleich dem linken Rand der
 * Eintrags-Labels (x=20), damit beide Seiten optisch symmetrisch wirken. */
#define SCHIEBER_RAND          20
#define SCHIEBER_SCHWELLE_EIN  0.7f
#define SCHIEBER_SCHWELLE_AUS  0.3f
#define FARBE_SCHIEBER_AUS     0x4a5568
#define FARBE_SCHIEBER_EIN     0x3aa655
#define FARBE_SCHIEBER_KNOPF   0xffffff

#define TAGESFENSTER_ANZEIGEDAUER_MS (15 * 1000)
#define HEUTEFENSTER_INAKTIV_MS      (5 * 60 * 1000)

/* Titel (ICS_TITEL_MAX) plus Platz fuer "HH:MM  "-Prefix. */
#define ZEILE_MAX (ICS_TITEL_MAX + 16)

static lv_obj_t *s_scr;
static lv_obj_t *s_tag_buttons[TAGE_ANZAHL]; /* alle 7 Positionen sind jetzt Buttons */
static lv_obj_t *s_tag_labels[TAGE_ANZAHL];
static int s_letzter_tag_schluessel = -1;

/* Button, dessen Tages-/Heute-Fenster gerade offen ist - bekommt einen
 * hellblauen Rahmen, bis das Fenster wieder schliesst (siehe
 * aktiven_button_setzen). */
static lv_obj_t *s_aktiver_button;

static void aktiven_button_setzen(lv_obj_t *button)
{
    if (s_aktiver_button)
        lv_obj_set_style_border_width(s_aktiver_button, 0, 0);
    s_aktiver_button = button;
    if (s_aktiver_button) {
        lv_obj_set_style_border_width(s_aktiver_button, 3, 0);
        lv_obj_set_style_border_color(s_aktiver_button, lv_color_hex(FARBE_BUTTON_RAND_AKTIV), 0);
    }
}

static lv_obj_t *s_tages_fenster;
static lv_timer_t *s_tages_fenster_timer;
static lv_obj_t *s_heute_fenster;
static lv_timer_t *s_heute_fenster_timer;

typedef struct {
    lv_obj_t *spur;
    lv_obj_t *knopf;
    int index;
    bool an;
    int32_t min_x;
    int32_t max_x;
    int32_t press_start_x;
    int32_t knopf_start_x;
} schieber_t;

static schieber_t s_schieber[KALENDER_EINTRAEGE_MAX];

/* Textlabel neben jedem Schieberegler im Heute-Fenster, indiziert wie
 * s_schieber (derselbe Index wie kalender_anzeige_tablette_bestaetigen) -
 * wird beim Loslassen des Schiebereglers live aktualisiert (Haken/Graufaerbung),
 * ohne auf das naechste Neuoeffnen des Fensters warten zu muessen. */
static lv_obj_t *s_tabletten_zeile_labels[KALENDER_EINTRAEGE_MAX];

static void tagesfenster_intern_schliessen(void)
{
    if (s_tages_fenster_timer) {
        lv_timer_delete(s_tages_fenster_timer);
        s_tages_fenster_timer = NULL;
    }
    if (s_tages_fenster) {
        lv_obj_delete(s_tages_fenster);
        s_tages_fenster = NULL;
        aktiven_button_setzen(NULL);
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
        aktiven_button_setzen(NULL);
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

/* Baut das gemeinsame Grundgeruest (dunkles Panel + zweizeiliger Kopf +
 * "X"-Schliessen-Button) fuer beide Fenstertypen. Muss innerhalb eines
 * bereits gehaltenen LVGL-Locks aufgerufen werden (siehe Aufrufer unten).
 *
 * Der Kopf ist bewusst in zwei kurze Zeilen aufgeteilt (Wochentag/"HEUTE"
 * oben, Datum darunter) statt einer langen kombinierten Zeile - so bleibt
 * links genug Platz, und das "X" kann in die tatsaechliche Ecke wandern,
 * ohne den Text zu ueberlagern (frueher ueberschnitt das "X" bei langen
 * Titeln wie "DIENSTAG, 14. Juli 2026" die letzten Ziffern). */
static lv_obj_t *fenster_grundgeruest_erzeugen(const char *titel_oben, const char *titel_unten,
                                               int32_t hoehe, lv_event_cb_t schliessen_cb)
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

    lv_obj_t *kopf_oben = lv_label_create(panel);
    lv_label_set_text(kopf_oben, titel_oben);
    lv_obj_set_style_text_font(kopf_oben, &schrift_mittel_40, 0);
    lv_obj_set_style_text_color(kopf_oben, lv_color_hex(FARBE_FENSTER_TEXT), 0);
    lv_obj_set_pos(kopf_oben, 20, 8);

    lv_obj_t *kopf_unten = lv_label_create(panel);
    lv_label_set_text(kopf_unten, titel_unten);
    lv_obj_set_style_text_font(kopf_unten, &schrift_klein_28, 0);
    lv_obj_set_style_text_color(kopf_unten, lv_color_hex(FARBE_FENSTER_TEXT), 0);
    lv_obj_set_pos(kopf_unten, 20, 54);

    lv_obj_t *btn_schliessen = lv_button_create(panel);
    lv_obj_set_size(btn_schliessen, 40, 40);
    lv_obj_set_style_bg_color(btn_schliessen, lv_color_hex(FARBE_BUTTON_HINTERGRUND), 0);
    lv_obj_set_style_bg_opa(btn_schliessen, LV_OPA_COVER, 0);
    lv_obj_align(btn_schliessen, LV_ALIGN_TOP_RIGHT, -6, 6);
    lv_obj_add_event_cb(btn_schliessen, schliessen_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *x_label = lv_label_create(btn_schliessen);
    lv_label_set_text(x_label, "X");
    lv_obj_set_style_text_font(x_label, &schrift_klein_28, 0);
    lv_obj_set_style_text_color(x_label, lv_color_hex(FARBE_BUTTON_TEXT), 0);
    lv_obj_center(x_label);

    return panel;
}

/* Abgehakte Tabletten bekommen dieses ASCII-Praefix statt eines Unicode-
 * Hakens - Montserrat-Bold enthaelt keine Symbolglyphen wie U+2713,
 * lv_font_conv bricht dafuer ab (siehe tools/fonts/erzeuge_fonts.ps1 und
 * app_main.c/UEBERSICHT_HAKEN_PRAEFIX). */
#define TAGESANSICHT_HAKEN_PRAEFIX "[x] "

static void eintrag_zeile_formatieren(const kalender_tag_eintrag_t *e, bool haken, char *ziel, size_t ziel_groesse)
{
    /* Explizite Praezision (dynamisch aus der Zielgroesse) statt nacktem
     * "%s": ueber einen Zeiger zugegriffene Array-Felder (e->titel)
     * verlieren bei GCCs Format-Truncation-Pruefung ihre bekannte Groesse,
     * wodurch ein sehr grosses Worst-Case-Ergebnis angenommen und
     * -Werror=format-truncation ausgeloest wird. */
    const char *praefix = haken ? TAGESANSICHT_HAKEN_PRAEFIX : "";
    int praefix_laenge = (int)strlen(praefix);
    if (e->ganztags)
        snprintf(ziel, ziel_groesse, "%s%.*s", praefix, (int)ziel_groesse - 1 - praefix_laenge, e->titel);
    else
        snprintf(ziel, ziel_groesse, "%s%02d:%02d  %.*s", praefix, e->stunde, e->minute,
                 (int)ziel_groesse - 8 - praefix_laenge, e->titel);
}

/* ---- Tages-Fenster (read-only, 15s) --------------------------------- */

/* Spalten wie im Hauptbildschirm (Tabletten links, Termine rechts) statt
 * einer gemeinsamen Liste - konsistent zur Hauptanzeige und ohne
 * Scroll-Geste auskommend. Damit bei vielen Eintraegen an einem Tag
 * nichts unten aus dem Fenster herauslaeuft, wird pro Spalte auf
 * TAGESFENSTER_ZEILEN_MAX begrenzt, der Rest als "+N weitere" angezeigt. */
#define TAGESFENSTER_SPALTE_BREITE 270
#define TAGESFENSTER_SPALTE_X_LINKS  20
#define TAGESFENSTER_SPALTE_X_RECHTS (FENSTER_BREITE - TAGESFENSTER_SPALTE_BREITE - 20)
#define TAGESFENSTER_ZEILEN_MAX 5

static void tagesfenster_spalte_zeichnen(lv_obj_t *parent, int32_t x, const char *ueberschrift,
                                         const kalender_tag_eintrag_t *eintraege, int anzahl_gesamt,
                                         bool tabletten_spalte, bool tag_vergangen)
{
    int32_t y = 100;

    lv_obj_t *kopf = lv_label_create(parent);
    lv_label_set_text(kopf, ueberschrift);
    lv_obj_set_style_text_font(kopf, &schrift_klein_28, 0);
    lv_obj_set_style_text_color(kopf, lv_color_hex(FARBE_FENSTER_AKZENT), 0);
    lv_obj_set_pos(kopf, x, y);
    y += 34;

    int gefiltert = 0;
    for (int i = 0; i < anzahl_gesamt; i++)
        if (eintraege[i].ist_tablette == tabletten_spalte)
            gefiltert++;

    if (gefiltert == 0) {
        lv_obj_t *label = lv_label_create(parent);
        lv_label_set_text(label, tabletten_spalte ? "Keine Tabletten." : "Keine Termine.");
        lv_obj_set_style_text_font(label, &schrift_klein_28, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(FARBE_FENSTER_TEXT), 0);
        lv_obj_set_pos(label, x, y);
        return;
    }

    int gezeigt = 0;
    for (int i = 0; i < anzahl_gesamt; i++) {
        if (eintraege[i].ist_tablette != tabletten_spalte)
            continue;
        if (gezeigt >= TAGESFENSTER_ZEILEN_MAX - (gefiltert > TAGESFENSTER_ZEILEN_MAX ? 1 : 0))
            break;

        bool abgehakt = tabletten_spalte && eintraege[i].bestaetigt;
        char inhalt[ZEILE_MAX];
        eintrag_zeile_formatieren(&eintraege[i], abgehakt, inhalt, sizeof inhalt);

        lv_obj_t *label = lv_label_create(parent);
        lv_label_set_text(label, inhalt);
        lv_obj_set_style_text_font(label, &schrift_klein_28, 0);
        bool vergangen = tag_vergangen && !tabletten_spalte;
        bool gedaempft = vergangen || abgehakt;
        lv_obj_set_style_text_color(label, lv_color_hex(gedaempft ? FARBE_VERGANGEN : FARBE_FENSTER_TEXT), 0);
        if (vergangen)
            lv_obj_set_style_text_decor(label, LV_TEXT_DECOR_STRIKETHROUGH, 0);
        lv_obj_set_pos(label, x, y);
        y += 36;
        gezeigt++;
    }

    if (gefiltert > gezeigt) {
        lv_obj_t *mehr = lv_label_create(parent);
        char text[24];
        snprintf(text, sizeof text, "+%d weitere", gefiltert - gezeigt);
        lv_label_set_text(mehr, text);
        lv_obj_set_style_text_font(mehr, &schrift_klein_28, 0);
        lv_obj_set_style_text_color(mehr, lv_color_hex(FARBE_VERGANGEN), 0);
        lv_obj_set_pos(mehr, x, y);
    }
}

static void tages_fenster_oeffnen(int tage_versatz, lv_obj_t *button)
{
    kalender_tag_eintrag_t eintraege[KALENDER_EINTRAEGE_MAX];
    int anzahl = kalender_anzeige_eintraege_fuer_tag(tage_versatz, eintraege, KALENDER_EINTRAEGE_MAX);

    time_t tag_zeit = time(NULL) + (time_t)tage_versatz * 86400;
    struct tm lokal;
    localtime_r(&tag_zeit, &lokal);
    char datum[32];
    zeit_datum_text(&lokal, datum, sizeof datum);

    /* Von den ueber die Wochentag-Buttons erreichbaren Tagen (gestern..+5)
     * liegt nur "gestern" komplett in der Vergangenheit - eine
     * Uhrzeit-genaue Pruefung pro Eintrag ist hier also nicht noetig. */
    bool tag_vergangen = tage_versatz < 0;

    lvgl_port_lock(0);
    tagesfenster_intern_schliessen();
    heutefenster_intern_schliessen(); /* nur ein Fenster gleichzeitig */

    s_tages_fenster = fenster_grundgeruest_erzeugen(zeit_wochentag_gross(&lokal), datum,
                                                     FENSTER_HOEHE_TAG, tagesfenster_schliessen_cb);
    aktiven_button_setzen(button);

    tagesfenster_spalte_zeichnen(s_tages_fenster, TAGESFENSTER_SPALTE_X_LINKS, "TABLETTEN",
                                 eintraege, anzahl, true, tag_vergangen);
    tagesfenster_spalte_zeichnen(s_tages_fenster, TAGESFENSTER_SPALTE_X_RECHTS, "TERMINE",
                                 eintraege, anzahl, false, tag_vergangen);

    s_tages_fenster_timer = lv_timer_create(tagesfenster_timer_cb, TAGESFENSTER_ANZEIGEDAUER_MS, NULL);
    lv_timer_set_repeat_count(s_tages_fenster_timer, 1);
    lvgl_port_unlock();
}

static void tag_button_cb(lv_event_t *e)
{
    int versatz = (int)(intptr_t)lv_event_get_user_data(e);
    tages_fenster_oeffnen(versatz, lv_event_get_target(e));
}

/* ---- Heute-Fenster (mit Bestaetigungs-Schaltern, 5min Inaktivitaet) - */

static void schieber_stellung_anwenden(schieber_t *s, bool an)
{
    s->an = an;
    lv_obj_set_style_bg_color(s->spur, lv_color_hex(an ? FARBE_SCHIEBER_EIN : FARBE_SCHIEBER_AUS), 0);
    lv_obj_set_x(s->knopf, an ? s->max_x : s->min_x);
}

static void schieber_pressed_cb(lv_event_t *e)
{
    schieber_t *s = (schieber_t *)lv_event_get_user_data(e);
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    s->press_start_x = p.x;
    s->knopf_start_x = lv_obj_get_x(s->knopf);
}

static void schieber_pressing_cb(lv_event_t *e)
{
    schieber_t *s = (schieber_t *)lv_event_get_user_data(e);
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int32_t neu_x = s->knopf_start_x + (p.x - s->press_start_x);
    if (neu_x < s->min_x)
        neu_x = s->min_x;
    if (neu_x > s->max_x)
        neu_x = s->max_x;
    lv_obj_set_x(s->knopf, neu_x);
}

/* Blendet das "[x] "-Praefix ins zugehoerige Zeilenlabel ein/aus und faerbt
 * es passend - direkt am Loslassen des Schiebereglers, ohne auf ein
 * Neuoeffnen des Fensters warten zu muessen. Schneidet/ergaenzt das
 * Praefix am VORHANDENEN Label-Text, statt den Eintrag erneut abzufragen -
 * einfacher als eine zweite Kalender-Abfrage nur fuer die Beschriftung. */
static void tabletten_zeile_aktualisieren(int index, bool bestaetigt)
{
    lv_obj_t *label = s_tabletten_zeile_labels[index];
    if (!label)
        return;

    const char *praefix = TAGESANSICHT_HAKEN_PRAEFIX;
    size_t praefix_laenge = strlen(praefix);
    const char *aktuell = lv_label_get_text(label);
    bool hat_praefix = strncmp(aktuell, praefix, praefix_laenge) == 0;

    if (bestaetigt && !hat_praefix) {
        char neu[ZEILE_MAX];
        snprintf(neu, sizeof neu, "%s%s", praefix, aktuell);
        lv_label_set_text(label, neu);
    } else if (!bestaetigt && hat_praefix) {
        /* NIEMALS einen Zeiger in den eigenen Label-Puffer an
         * lv_label_set_text uebergeben: die Funktion realloziert zuerst
         * genau diesen Puffer und kopiert dann aus der (damit ggf. schon
         * freigegebenen/verschobenen) Quelle - Ergebnis waren wirre
         * Zeichen wie "'_?0 Frueh" nach dem Zurueckschieben des
         * Tabletten-Schiebers. Erst in einen lokalen Puffer kopieren
         * (gleiche Fehlerklasse wie der Dropdown-Fallstrick in
         * einrichtung.c/wlan_scan_tick_cb). */
        char neu[ZEILE_MAX];
        snprintf(neu, sizeof neu, "%s", aktuell + praefix_laenge);
        lv_label_set_text(label, neu);
    }
    lv_obj_set_style_text_color(label, lv_color_hex(bestaetigt ? FARBE_VERGANGEN : FARBE_FENSTER_TEXT), 0);
}

/* Entscheidet beim Loslassen, ob genug gezogen wurde: nur jenseits der
 * Schwellen wird umgeschaltet, dazwischen schnappt der Knopf zurueck. */
static void schieber_released_cb(lv_event_t *e)
{
    schieber_t *s = (schieber_t *)lv_event_get_user_data(e);
    float anteil = (float)(lv_obj_get_x(s->knopf) - s->min_x) / (float)(s->max_x - s->min_x);
    bool neu_an = s->an;
    if (anteil >= SCHIEBER_SCHWELLE_EIN)
        neu_an = true;
    else if (anteil <= SCHIEBER_SCHWELLE_AUS)
        neu_an = false;
    schieber_stellung_anwenden(s, neu_an);
    kalender_anzeige_tablette_bestaetigen(s->index, neu_an);
    tabletten_zeile_aktualisieren(s->index, neu_an);

    lvgl_port_lock(0);
    if (s_heute_fenster_timer)
        lv_timer_reset(s_heute_fenster_timer);
    lvgl_port_unlock();
}

/* Rechtsbuendig per lv_obj_align statt manuell aus FENSTER_BREITE
 * errechnetem x: Ein Panel behaelt ohne lv_obj_remove_style_all() sein
 * Standard-Innenpolster, wodurch von links UND rechts aus errechnete
 * Koordinaten unterschiedlich stark verschoben werden koennen (aehnlich
 * dem Rand-Insets-Fallstrick der Status-Symbole, siehe app_main.c).
 * lv_obj_align kennt die tatsaechliche rechte Kante des Panels und ist
 * dagegen immun - min_x/max_x werden danach aus der tatsaechlich
 * aufgeloesten Position ausgelesen (lv_obj_get_x), damit der separate
 * Knopf (dasselbe Elternobjekt) exakt dazu passt. */
static void schieber_erzeugen(lv_obj_t *parent, int32_t y, int index, bool an)
{
    schieber_t *s = &s_schieber[index];
    s->index = index;
    s->an = an;

    lv_obj_t *spur = lv_obj_create(parent);
    lv_obj_remove_style_all(spur);
    lv_obj_remove_flag(spur, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(spur, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(spur, SCHIEBER_BREITE, SCHIEBER_HOEHE);
    /* Das Fenster-Panel ist gerade erst erzeugt worden - ohne diesen
     * erzwungenen Layout-Durchlauf sind seine Koordinaten zum Zeitpunkt
     * von lv_obj_align/lv_obj_get_x noch nicht aufgeloest (parent-Rahmen
     * effektiv [0..-1]), wodurch die Ausrichtung faelschlich nahe x=0
     * statt rechtsbuendig landete. */
    lv_obj_update_layout(parent);
    lv_obj_align(spur, LV_ALIGN_TOP_RIGHT, -SCHIEBER_RAND, y);
    lv_obj_update_layout(parent);
    s->min_x = lv_obj_get_x(spur);
    s->max_x = s->min_x + SCHIEBER_TRAVEL;
    lv_obj_set_style_radius(spur, SCHIEBER_HOEHE / 2, 0);
    lv_obj_set_style_bg_opa(spur, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(spur, lv_color_hex(an ? FARBE_SCHIEBER_EIN : FARBE_SCHIEBER_AUS), 0);
    s->spur = spur;

    lv_obj_t *knopf = lv_obj_create(parent);
    lv_obj_remove_style_all(knopf);
    lv_obj_remove_flag(knopf, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(knopf, SCHIEBER_KNOPF, SCHIEBER_KNOPF);
    lv_obj_set_pos(knopf, an ? s->max_x : s->min_x, y + (SCHIEBER_HOEHE - SCHIEBER_KNOPF) / 2);
    lv_obj_set_style_radius(knopf, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(knopf, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(knopf, lv_color_hex(FARBE_SCHIEBER_KNOPF), 0);
    s->knopf = knopf;

    lv_obj_add_event_cb(knopf, schieber_pressed_cb, LV_EVENT_PRESSED, s);
    lv_obj_add_event_cb(knopf, schieber_pressing_cb, LV_EVENT_PRESSING, s);
    lv_obj_add_event_cb(knopf, schieber_released_cb, LV_EVENT_RELEASED, s);
    lv_obj_add_event_cb(knopf, schieber_released_cb, LV_EVENT_PRESS_LOST, s);
}

static void heutefenster_hintergrund_cb(lv_event_t *e)
{
    (void)e;
    lvgl_port_lock(0);
    if (s_heute_fenster_timer)
        lv_timer_reset(s_heute_fenster_timer);
    lvgl_port_unlock();
}

static void heute_oeffnen_intern(lv_obj_t *aktiver_button)
{
    kalender_tag_eintrag_t eintraege[KALENDER_EINTRAEGE_MAX];
    int anzahl = kalender_anzeige_heutige_eintraege(eintraege, KALENDER_EINTRAEGE_MAX);

    time_t jetzt = time(NULL);
    struct tm lokal;
    localtime_r(&jetzt, &lokal);
    char datum[32];
    zeit_datum_text(&lokal, datum, sizeof datum);
    int jetzt_minuten = lokal.tm_hour * 60 + lokal.tm_min;

    lvgl_port_lock(0);
    heutefenster_intern_schliessen();
    tagesfenster_intern_schliessen(); /* nur ein Fenster gleichzeitig */

    s_heute_fenster = fenster_grundgeruest_erzeugen("HEUTE", datum, FENSTER_HOEHE_HEUTE, heutefenster_schliessen_cb);
    aktiven_button_setzen(aktiver_button);
    lv_obj_add_event_cb(s_heute_fenster, heutefenster_hintergrund_cb, LV_EVENT_PRESSED, NULL);

    int32_t y = 100;
    bool tablette_vorhanden = false;
    for (int i = 0; i < anzahl; i++) {
        if (!eintraege[i].ist_tablette)
            continue;
        tablette_vorhanden = true;

        char inhalt[ZEILE_MAX];
        eintrag_zeile_formatieren(&eintraege[i], eintraege[i].bestaetigt, inhalt, sizeof inhalt);

        lv_obj_t *label = lv_label_create(s_heute_fenster);
        lv_label_set_text(label, inhalt);
        lv_obj_set_style_text_font(label, &schrift_klein_28, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(eintraege[i].bestaetigt ? FARBE_VERGANGEN : FARBE_FENSTER_TEXT), 0);
        lv_obj_set_pos(label, 20, y + 12);
        s_tabletten_zeile_labels[i] = label;

        schieber_erzeugen(s_heute_fenster, y, i, eintraege[i].bestaetigt);

        y += 70;
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
        eintrag_zeile_formatieren(&eintraege[i], false, inhalt, sizeof inhalt);
        bool vergangen = !eintraege[i].ganztags &&
                          (eintraege[i].stunde * 60 + eintraege[i].minute) < jetzt_minuten;
        lv_obj_t *label = lv_label_create(s_heute_fenster);
        lv_label_set_text(label, inhalt);
        lv_obj_set_style_text_font(label, &schrift_klein_28, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(vergangen ? FARBE_VERGANGEN : FARBE_FENSTER_TEXT), 0);
        if (vergangen)
            lv_obj_set_style_text_decor(label, LV_TEXT_DECOR_STRIKETHROUGH, 0);
        lv_obj_set_pos(label, 20, y + 8);
        y += 36;
    }

    s_heute_fenster_timer = lv_timer_create(heutefenster_timer_cb, HEUTEFENSTER_INAKTIV_MS, NULL);
    lvgl_port_unlock();
}

static void heute_button_cb(lv_event_t *e)
{
    heute_oeffnen_intern(lv_event_get_target(e));
}

void tagesansicht_heute_oeffnen(void)
{
    heute_oeffnen_intern(s_tag_buttons[HEUTE_INDEX]);
}

bool tagesansicht_fenster_offen(void)
{
    return s_tages_fenster != NULL || s_heute_fenster != NULL;
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
        bool ist_heute = (versatz == 0);

        lv_obj_t *btn = lv_button_create(scr);
        button_grundstil(btn);
        if (ist_heute) {
            /* Breite passt sich per LV_SIZE_CONTENT exakt an "Heute" an -
             * keine feste Breite raten muessen, die zufaellig gerade so
             * (nicht) reicht. */
            lv_obj_set_size(btn, LV_SIZE_CONTENT, BUTTON_HOEHE);
            lv_obj_set_style_pad_left(btn, HEUTE_BTN_PAD, 0);
            lv_obj_set_style_pad_right(btn, HEUTE_BTN_PAD, 0);
        } else {
            lv_obj_set_size(btn, BUTTON_BREITE, BUTTON_HOEHE);
        }
        lv_obj_set_pos(btn, SPALTE_X, y);
        lv_obj_add_event_cb(btn, ist_heute ? heute_button_cb : tag_button_cb, LV_EVENT_CLICKED,
                             ist_heute ? NULL : (void *)(intptr_t)versatz);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, ist_heute ? "Heute" : "...");
        lv_obj_set_style_text_font(label, &schrift_klein_28, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(FARBE_BUTTON_TEXT), 0);
        lv_obj_center(label);

        s_tag_buttons[i] = btn;
        s_tag_labels[i] = label;
    }
}

/* Faerbt die Beschriftung eines Wochentag-Buttons nach der Anzahl Termine
 * (nicht Tabletten) an diesem Tag - auf einen Blick erkennbar, ohne das
 * Tages-/Heute-Fenster oeffnen zu muessen. */
static void button_terminfarbe_setzen(lv_obj_t *label, int tage_versatz)
{
    kalender_tag_eintrag_t eintraege[KALENDER_EINTRAEGE_MAX];
    int anzahl = kalender_anzeige_eintraege_fuer_tag(tage_versatz, eintraege, KALENDER_EINTRAEGE_MAX);
    int termine = 0;
    for (int i = 0; i < anzahl; i++)
        if (!eintraege[i].ist_tablette)
            termine++;

    uint32_t farbe = FARBE_BUTTON_TEXT;
    if (termine == 1)
        farbe = FARBE_TERMIN_1;
    else if (termine == 2)
        farbe = FARBE_TERMIN_2;
    else if (termine >= 3)
        farbe = FARBE_TERMIN_3PLUS;

    lv_obj_set_style_text_color(label, lv_color_hex(farbe), 0);
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
        if (versatz != 0) {
            time_t tag_zeit = jetzt + (time_t)versatz * 86400;
            struct tm tag_lokal;
            localtime_r(&tag_zeit, &tag_lokal);
            lv_label_set_text(s_tag_labels[i], zeit_wochentag_kurz(&tag_lokal));
        }
        button_terminfarbe_setzen(s_tag_labels[i], versatz);
    }
    lvgl_port_unlock();
}

void tagesansicht_sichtbarkeit_setzen(bool sichtbar)
{
    for (int i = 0; i < TAGE_ANZAHL; i++) {
        if (sichtbar)
            lv_obj_remove_flag(s_tag_buttons[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_tag_buttons[i], LV_OBJ_FLAG_HIDDEN);
    }
}
