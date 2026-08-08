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
/* Zeilenhoehen im "Heute"-Fenster - dieselben Werte tauchen im Zeichnen
 * (heute_oeffnen_intern) mehrfach auf, deshalb hier einmal benannt statt
 * als Zauberzahl. */
/* Kopfhoehe fuer den EINZEILIGEN Kopf ("HEUTE  8. August 2026" nebeneinander,
 * siehe fenster_grundgeruest_erzeugen mit titel_einzeilig=true): Titel steht
 * bei y=8 und ist gut 40px hoch, darunter etwas Luft. Der "X"-Button (40px
 * hoch ab y=6) passt ebenfalls hinein. */
#define HEUTE_KOPF_HOEHE            62
#define HEUTE_ZEILE_TABLETTE_HOEHE  70
#define HEUTE_LEER_HINWEIS_HOEHE    45  /* "Keine Tabletten heute." */
#define HEUTE_TERMINE_KOPF_HOEHE    38
#define HEUTE_ZEILE_TERMIN_HOEHE    36
/* Erste Version passte die Fenster-Hoehe an die Eintrags-Anzahl an - bei
 * vielen Eintraegen (live beobachtet: 4 Tabletten an einem Tag) wirkte das
 * trotzdem gequetscht (letzte Zeile klebte direkt an OK/Abbrechen). Peters
 * Entscheidung: fester Fensterrahmen, Inhalt scrollt bei Bedarf - so bleibt
 * bei WENIGEN Eintraegen viel Luft und bei VIELEN wird nichts gequetscht.
 * Sichtbare Listenhoehe als exaktes Vielfaches der Zeilenhoehe, damit die
 * letzte sichtbare Zeile buendig endet statt angeschnitten; vier Zeilen
 * passen, seit der Kopf nur noch einzeilig ist (siehe HEUTE_KOPF_HOEHE). */
#define HEUTE_INHALT_SICHTBAR_HOEHE (4 * HEUTE_ZEILE_TABLETTE_HOEHE)
/* Abstand zwischen Listen-Unterkante und OK/Abbrechen - es soll IMMER ein
 * schwarzer Streifen dazwischen bleiben, damit eine angeschnittene Zeile am
 * unteren Listenrand nie in die Buttons hineinlaeuft (Peters Rueckmeldung). */
#define HEUTE_INHALT_ABSTAND_UNTEN 20
/* Benoetigte INHALTS-Hoehe (ohne Rahmen/Innenpolster des Panels). Die
 * tatsaechliche Fensterhoehe wird daraus zur Laufzeit errechnet, siehe
 * heute_oeffnen_intern - das Panel hat ein Standard-Innenpolster von rund
 * 22px je Seite, das frueher genau die hier eingeplanten Abstaende auffrass
 * (live: letzte Zeile schnitt den OK-Button an, und die Liste galt faelschlich
 * als "passt genau" und war deshalb gar nicht scrollbar). */
#define HEUTE_INHALT_GESAMT_HOEHE (HEUTE_KOPF_HOEHE + HEUTE_INHALT_SICHTBAR_HOEHE + \
                                   HEUTE_INHALT_ABSTAND_UNTEN + OK_ABBRECHEN_HOEHE + \
                                   OK_ABBRECHEN_RAND_UNTEN)
/* Rechts reservierter Streifen fuer die Scrollleiste - als pad_right des
 * Listenbereichs umgesetzt, damit die rechtsbuendigen Checkboxen davor enden
 * und die Leiste sie nie ueberlagert. Bewusst breit: eine duenne
 * Standard-Leiste war auf dem Geraet kaum als Bedienelement erkennbar. */
#define HEUTE_SCROLLLEISTE_BREITE 22
/* Rechter Rand des "X"-Schliessen-Buttons im "Heute"-Fenster, so gewaehlt,
 * dass seine rechte Kante exakt mit der rechten Kante der Checkboxen
 * abschliesst (Peters Wunsch): die Checkboxen enden um den Scrollleisten-
 * Streifen plus CHECKBOX_RAND vor dem rechten Inhaltsrand. */
#define HEUTE_X_BUTTON_RAND (HEUTE_SCROLLLEISTE_BREITE + CHECKBOX_RAND)
/* Uebliche Ecke fuer alle anderen Fenster (ohne Liste/Checkboxen). */
#define X_BUTTON_RAND_STANDARD 6
/* Senkrechtes Innenpolster des "Heute"-Panels, bewusst kleiner als der
 * LVGL-Standard (rund 20px je Seite): mit vier sichtbaren Listenzeilen kam
 * das Fenster sonst auf 474px und liess bei 480px Bildschirmhoehe nur noch
 * 3px Rand - es wirkte randlos. Nur dieses Fenster ist betroffen, die
 * uebrigen behalten das Standardpolster. */
#define HEUTE_PANEL_POLSTER_V 8
#define FARBE_FENSTER_TEXT   0xffffff
#define FARBE_FENSTER_AKZENT 0xffd75f
#define FARBE_VERGANGEN      0x707a8a /* gedaempftes Grau fuer bereits vergangene Termine */
#define FARBE_TABLETTE_FAELLIG      FARBE_FENSTER_AKZENT /* faellig, noch unbestaetigt: dasselbe Gold wie sonst im Fenster */
#define FARBE_TABLETTE_UEBERFAELLIG 0xff5a4a /* seit KALENDER_TABLETTE_UEBERFAELLIG_MIN unbestaetigt: dasselbe Rot wie die Status-Symbole in app_main.c */

/* Checkbox statt Schieber (Ausbaustufe 2, Peters Test-Wunsch: "ich muss die
 * Komplexitaet rausnehmen") - ein Tipp aendert NUR den Anzeigezustand, NICHT
 * sofort den bestaetigten Status wie vormals der Schieber. Erst der
 * OK-Button (siehe ok_abbrechen_erzeugen) uebernimmt alle angehakten
 * Checkboxen auf einmal; Abbrechen/"X"/Timeout verwerfen sie wieder. Ein
 * einzelner Fehltipp ist dadurch folgenlos korrigierbar - der urspruengliche
 * Schieber-Gedanke ("keine zufaellige Beruehrung darf eine Tablette
 * faelschlich abhaken") bleibt so gewahrt, nur ohne die Zieh-Geste. */
#define CHECKBOX_GROESSE 46 /* mind. so gross wie vormals der Schieber-Knopf */
/* Abstand zum rechten Fensterrand - bewusst gleich dem linken Rand der
 * Eintrags-Labels (x=20), damit beide Seiten optisch symmetrisch wirken. */
#define CHECKBOX_RAND     20
#define FARBE_SCHIEBER_AUS 0x4a5568 /* auch fuer die leere Checkbox und den Update-Fortschrittsbalken */
#define FARBE_SCHIEBER_EIN 0x3aa655 /* auch fuer die angehakte Checkbox, den OK-Button und den Update-Fortschrittsbalken */
#define FARBE_ABBRECHEN     FARBE_TABLETTE_UEBERFAELLIG /* dasselbe Rot wie ueberfaellige Tabletten */

#define OK_ABBRECHEN_BREITE 150
#define OK_ABBRECHEN_HOEHE  52
#define OK_ABBRECHEN_RAND_UNTEN 16

#define TAGESFENSTER_ANZEIGEDAUER_MS (15 * 1000)
#define HEUTEFENSTER_INAKTIV_MS      (5 * 60 * 1000)
/* Erinnerungsfenster: bewusst kurz. Reagiert niemand, verschwindet es
 * wieder und gibt Uhrzeit/Datum frei - das erneute Melden uebernimmt
 * app_main.c (siehe erinnerung_pruefen). Ein dauerhaft stehendes Fenster
 * waere derselbe Fehler wie eine nie quittierte Fehlermeldung: es verdeckt
 * genau die Anzeige, um die es beim Geraet eigentlich geht. */
#define ERINNERUNG_ANZEIGEDAUER_MS   (90 * 1000)
/* Zeilenhoehe pro Tablette (Name + optionale Beschreibung + Checkbox) und
 * maximale Anzahl gleichzeitig sichtbarer Zeilen - bei mehr Tabletten als
 * ERINNERUNG_ZEILEN_MAX zeigt die letzte Zeile "+N weitere" (siehe
 * tagesfenster_spalte_zeichnen fuer dasselbe Muster). Fenster-Hoehe deckt
 * Kopf (100px) + ERINNERUNG_ZEILEN_MAX Zeilen + OK/Abbrechen-Zeile. */
#define ERINNERUNG_ZEILE_HOEHE       78
#define ERINNERUNG_ZEILEN_MAX        3
#define FENSTER_HOEHE_ERINNERUNG     (100 + ERINNERUNG_ZEILEN_MAX * ERINNERUNG_ZEILE_HOEHE + 90)

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
static lv_obj_t *s_erinnerung_fenster;
static lv_timer_t *s_erinnerung_fenster_timer;

typedef struct {
    lv_obj_t *box; /* NULL, solange kein Fenster diesen Index gerade anzeigt */
    int index;
} tablette_checkbox_t;

/* Indiziert wie kalender_anzeige_tablette_bestaetigen() - von Heute-Fenster
 * UND Erinnerungsfenster gemeinsam genutzt (nur eines von beiden ist je
 * offen, siehe die jeweiligen intern_schliessen()-Aufrufe). s_pending haelt
 * den Anzeigezustand bis zum OK-Tipp fest, unabhaengig vom tatsaechlich
 * bestaetigten Status (siehe Kommentar bei CHECKBOX_GROESSE). */
static tablette_checkbox_t s_checkboxen[KALENDER_EINTRAEGE_MAX];
static bool s_pending[KALENDER_EINTRAEGE_MAX];

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

/* Die Checkboxen gehoeren dem gerade geschlossenen Fenster und sind mit ihm
 * geloescht - die Zeiger MUESSEN genullt werden. Sonst wuerde ein
 * OK-Tipp im naechsten Fenster (Heute-Fenster und Erinnerungsfenster nutzen
 * dieselbe Index-Tabelle) versehentlich auf freigegebenen Speicher zugreifen
 * bzw. laengst geschlossene Zeilen mit uebernehmen. */
static void checkboxen_vergessen(void)
{
    for (int i = 0; i < KALENDER_EINTRAEGE_MAX; i++)
        s_checkboxen[i].box = NULL;
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
        checkboxen_vergessen();
        aktiven_button_setzen(NULL);
    }
}

static void erinnerung_intern_schliessen(void)
{
    if (s_erinnerung_fenster_timer) {
        lv_timer_delete(s_erinnerung_fenster_timer);
        s_erinnerung_fenster_timer = NULL;
    }
    if (s_erinnerung_fenster) {
        lv_obj_delete(s_erinnerung_fenster);
        s_erinnerung_fenster = NULL;
        checkboxen_vergessen();
    }
}

static void tagesfenster_timer_cb(lv_timer_t *t)
{
    (void)t;
    lvgl_port_lock(0);
    tagesfenster_intern_schliessen();
    lvgl_port_unlock();
}

static void erinnerung_timer_cb(lv_timer_t *t)
{
    (void)t;
    lvgl_port_lock(0);
    erinnerung_intern_schliessen();
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

static void erinnerung_schliessen_cb(lv_event_t *e)
{
    (void)e;
    lvgl_port_lock(0);
    erinnerung_intern_schliessen();
    lvgl_port_unlock();
}

/* Baut das gemeinsame Grundgeruest (dunkles Panel + Kopf +
 * "X"-Schliessen-Button) fuer beide Fenstertypen. Muss innerhalb eines
 * bereits gehaltenen LVGL-Locks aufgerufen werden (siehe Aufrufer unten).
 *
 * Der Kopf ist normalerweise in zwei kurze Zeilen aufgeteilt (Wochentag/
 * "HEUTE" oben, Datum darunter) statt einer langen kombinierten Zeile - so
 * bleibt links genug Platz, und das "X" kann in die tatsaechliche Ecke
 * wandern, ohne den Text zu ueberlagern (frueher ueberschnitt das "X" bei
 * langen Titeln wie "DIENSTAG, 14. Juli 2026" die letzten Ziffern).
 *
 * Mit "titel_einzeilig" stehen beide Teile nebeneinander in EINER Zeile -
 * spart rund 40px Kopfhoehe, die im "Heute"-Fenster der scrollbaren Liste
 * zugutekommen (Peters Vorschlag). Nur fuer kurze Untertitel geeignet: der
 * Platz bis zum "X"-Button ist begrenzt, lange Hinweistexte wie im
 * Update-Fenster brauchen weiterhin die zweizeilige Variante. */
static lv_obj_t *fenster_grundgeruest_erzeugen(const char *titel_oben, const char *titel_unten,
                                               int32_t hoehe, lv_event_cb_t schliessen_cb,
                                               bool titel_einzeilig, int32_t x_button_rand)
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
    if (titel_einzeilig) {
        /* Rechts neben den Titel, optische Grundlinien angeglichen: der
         * kleinere Untertitel wird um die halbe Hoehendifferenz nach unten
         * gerueckt, damit beide Texte mittig zueinander stehen. Breite des
         * Titels erst nach lv_obj_update_layout() belastbar. */
        lv_obj_update_layout(panel);
        int32_t breite_oben = lv_obj_get_width(kopf_oben);
        int32_t hoehe_oben  = lv_obj_get_height(kopf_oben);
        int32_t hoehe_unten = lv_obj_get_height(kopf_unten);
        lv_obj_set_pos(kopf_unten, 20 + breite_oben + 16, 8 + (hoehe_oben - hoehe_unten) / 2);
    } else {
        lv_obj_set_pos(kopf_unten, 20, 54);
    }

    lv_obj_t *btn_schliessen = lv_button_create(panel);
    lv_obj_set_size(btn_schliessen, 40, 40);
    lv_obj_set_style_bg_color(btn_schliessen, lv_color_hex(FARBE_BUTTON_HINTERGRUND), 0);
    lv_obj_set_style_bg_opa(btn_schliessen, LV_OPA_COVER, 0);
    lv_obj_align(btn_schliessen, LV_ALIGN_TOP_RIGHT, -x_button_rand, 6);
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
                                                     FENSTER_HOEHE_TAG, tagesfenster_schliessen_cb, false,
                                                     X_BUTTON_RAND_STANDARD);
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

static void checkbox_stellung_anwenden(lv_obj_t *box, bool markiert)
{
    lv_obj_set_style_bg_color(box, lv_color_hex(markiert ? FARBE_SCHIEBER_EIN : FARBE_SCHIEBER_AUS), 0);
}

/* Aendert NUR s_pending - der eigentliche Bestaetigungs-Aufruf
 * (kalender_anzeige_tablette_bestaetigen) passiert erst beim OK-Tipp, siehe
 * erinnerung_ok_cb/heute_ok_cb. Setzt trotzdem den jeweiligen
 * Inaktivitaets-Timer zurueck, wie zuvor beim Schieber-Loslassen. */
static void checkbox_geklickt_cb(lv_event_t *e)
{
    tablette_checkbox_t *c = (tablette_checkbox_t *)lv_event_get_user_data(e);
    s_pending[c->index] = !s_pending[c->index];
    checkbox_stellung_anwenden(c->box, s_pending[c->index]);

    lvgl_port_lock(0);
    if (s_heute_fenster_timer)
        lv_timer_reset(s_heute_fenster_timer);
    if (s_erinnerung_fenster_timer)
        lv_timer_reset(s_erinnerung_fenster_timer);
    lvgl_port_unlock();
}

/* Rechtsbuendig per lv_obj_align statt manuell aus FENSTER_BREITE
 * errechnetem x: Ein Panel behaelt ohne lv_obj_remove_style_all() sein
 * Standard-Innenpolster, wodurch von links UND rechts aus errechnete
 * Koordinaten unterschiedlich stark verschoben werden koennen (aehnlich
 * dem Rand-Insets-Fallstrick der Status-Symbole, siehe app_main.c).
 * lv_obj_align kennt die tatsaechliche rechte Kante des Panels und ist
 * dagegen immun. `y` ist die Y-Position innerhalb der jeweiligen Zeile. */
static void tablette_checkbox_erzeugen(lv_obj_t *parent, int32_t y, int index, bool markiert)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(box, CHECKBOX_GROESSE, CHECKBOX_GROESSE);
    lv_obj_update_layout(parent);
    lv_obj_align(box, LV_ALIGN_TOP_RIGHT, -CHECKBOX_RAND, y);
    lv_obj_set_style_radius(box, 8, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, 3, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(FARBE_FENSTER_TEXT), 0);
    checkbox_stellung_anwenden(box, markiert);

    s_checkboxen[index].box = box;
    s_checkboxen[index].index = index;
    s_pending[index] = markiert;

    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(box, checkbox_geklickt_cb, LV_EVENT_CLICKED, &s_checkboxen[index]);
}

/* Baut das gemeinsame gruene OK-/rote Abbrechen-Button-Paar unten im
 * Fenster - "Abbrechen" ist absichtlich gleichwertig zum "X" oben rechts
 * (schliessen_cb kann fuer beide denselben Callback verwenden). */
static void ok_abbrechen_erzeugen(lv_obj_t *parent, lv_event_cb_t ok_cb, lv_event_cb_t abbrechen_cb)
{
    /* Breite per LV_SIZE_CONTENT an die Beschriftung anpassen statt fest auf
     * OK_ABBRECHEN_BREITE - "Abbrechen" (9 Zeichen) passte bei diesem Projekt-
     * weit grossen Font (schrift_klein_28, seniorengerecht) bei fester Breite
     * nicht hinein und wurde zu "Abbrecher" abgeschnitten. OK_ABBRECHEN_BREITE
     * bleibt als Mindestbreite (per style_min_width) erhalten, damit der kurze
     * "OK"-Button trotzdem ein grosses, gut treffbares Touch-Ziel bleibt -
     * dasselbe Muster wie beim "Heute"-Tagesbutton (siehe HEUTE_BTN_PAD). */
    lv_obj_t *btn_abbrechen = lv_button_create(parent);
    lv_obj_set_size(btn_abbrechen, LV_SIZE_CONTENT, OK_ABBRECHEN_HOEHE);
    lv_obj_set_style_min_width(btn_abbrechen, OK_ABBRECHEN_BREITE, 0);
    lv_obj_set_style_pad_hor(btn_abbrechen, 16, 0);
    lv_obj_set_style_bg_color(btn_abbrechen, lv_color_hex(FARBE_ABBRECHEN), 0);
    lv_obj_align(btn_abbrechen, LV_ALIGN_BOTTOM_LEFT, 20, -OK_ABBRECHEN_RAND_UNTEN);
    lv_obj_add_event_cb(btn_abbrechen, abbrechen_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *abbrechen_label = lv_label_create(btn_abbrechen);
    lv_label_set_text(abbrechen_label, "Abbrechen");
    lv_obj_set_style_text_font(abbrechen_label, &schrift_klein_28, 0);
    lv_obj_center(abbrechen_label);

    lv_obj_t *btn_ok = lv_button_create(parent);
    lv_obj_set_size(btn_ok, LV_SIZE_CONTENT, OK_ABBRECHEN_HOEHE);
    lv_obj_set_style_min_width(btn_ok, OK_ABBRECHEN_BREITE, 0);
    lv_obj_set_style_pad_hor(btn_ok, 16, 0);
    lv_obj_set_style_bg_color(btn_ok, lv_color_hex(FARBE_SCHIEBER_EIN), 0);
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_RIGHT, -20, -OK_ABBRECHEN_RAND_UNTEN);
    lv_obj_add_event_cb(btn_ok, ok_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ok_label = lv_label_create(btn_ok);
    lv_label_set_text(ok_label, "OK");
    lv_obj_set_style_text_font(ok_label, &schrift_klein_28, 0);
    lv_obj_center(ok_label);
}

/* Uebernimmt fuer jede aktuell im Fenster gezeigte Checkbox (s_checkboxen[i].box
 * != NULL) den Pending-Zustand per kalender_anzeige_tablette_bestaetigen() -
 * gemeinsame Grundlage fuer den OK-Button beider Fenster. */
static void checkboxen_uebernehmen(void)
{
    for (int i = 0; i < KALENDER_EINTRAEGE_MAX; i++)
        if (s_checkboxen[i].box)
            kalender_anzeige_tablette_bestaetigen(i, s_pending[i]);
}

static void heutefenster_hintergrund_cb(lv_event_t *e)
{
    (void)e;
    lvgl_port_lock(0);
    if (s_heute_fenster_timer)
        lv_timer_reset(s_heute_fenster_timer);
    lvgl_port_unlock();
}

static void heute_ok_cb(lv_event_t *e)
{
    (void)e;
    checkboxen_uebernehmen();
    lvgl_port_lock(0);
    heutefenster_intern_schliessen();
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

    /* Einzeiliger Kopf ("HEUTE  8. August 2026") - der gesparte Platz kommt
     * der scrollbaren Liste darunter zugute (Peters Vorschlag). Das "X" wird
     * buendig zur rechten Checkbox-Kante gesetzt (HEUTE_X_BUTTON_RAND). */
    s_heute_fenster = fenster_grundgeruest_erzeugen("HEUTE", datum, HEUTE_INHALT_GESAMT_HOEHE,
                                                     heutefenster_schliessen_cb, true,
                                                     HEUTE_X_BUTTON_RAND);
    aktiven_button_setzen(aktiver_button);
    lv_obj_add_event_cb(s_heute_fenster, heutefenster_hintergrund_cb, LV_EVENT_PRESSED, NULL);

    /* Fensterhoehe so nachziehen, dass der INHALTSbereich exakt
     * HEUTE_INHALT_GESAMT_HOEHE hoch wird. Rahmen und Innenpolster des Panels
     * (rund 22px je Seite) werden dafuer zur Laufzeit gemessen statt geraten -
     * vorher frass genau dieses Polster die eingeplanten Abstaende auf, sodass
     * die letzte Zeile in den OK-Button lief. */
    lv_obj_set_style_pad_top(s_heute_fenster, HEUTE_PANEL_POLSTER_V, 0);
    lv_obj_set_style_pad_bottom(s_heute_fenster, HEUTE_PANEL_POLSTER_V, 0);
    lv_obj_update_layout(s_heute_fenster);
    int32_t rahmen_polster = lv_obj_get_height(s_heute_fenster) - lv_obj_get_content_height(s_heute_fenster);
    lv_obj_set_height(s_heute_fenster, HEUTE_INHALT_GESAMT_HOEHE + rahmen_polster);
    lv_obj_center(s_heute_fenster);
    lv_obj_update_layout(s_heute_fenster);

    /* Scrollbarer Inhaltsbereich zwischen Kopf und OK/Abbrechen - Fenster
     * bleibt fest, unabhaengig davon wie viele Tabletten/Termine an einem Tag
     * anfallen. Breite per Prozent (statt aus FENSTER_BREITE gerechnet), damit
     * das Panel-Innenpolster keine Rolle spielt; der Streifen fuer die
     * Scrollleiste entsteht als pad_right, wodurch die rechtsbuendigen
     * Checkboxen davor enden und nie von der Leiste ueberlagert werden. */
    lv_obj_t *inhalt = lv_obj_create(s_heute_fenster);
    lv_obj_remove_style_all(inhalt);
    lv_obj_set_pos(inhalt, 0, HEUTE_KOPF_HOEHE);
    lv_obj_set_size(inhalt, lv_pct(100), HEUTE_INHALT_SICHTBAR_HOEHE);
    lv_obj_set_style_pad_right(inhalt, HEUTE_SCROLLLEISTE_BREITE, 0);
    lv_obj_set_scroll_dir(inhalt, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(inhalt, LV_SCROLLBAR_MODE_AUTO);
    /* Deckender Hintergrund NUR fuer den Listenbereich: das Fenster selbst
     * ist bewusst leicht durchscheinend (LV_OPA_90, siehe
     * fenster_grundgeruest_erzeugen), wodurch die grosse Uhrzeit und die
     * Uebersicht-Ueberschriften mitten durch die Tabletten-Zeilen liefen -
     * live als unruhig/schlecht lesbar zurueckgemeldet. Die uebrigen Fenster
     * behalten ihre Transparenz, nur diese Liste wird undurchsichtig. */
    lv_obj_set_style_bg_color(inhalt, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(inhalt, LV_OPA_COVER, 0);
    /* Scrollleiste gross und deutlich - der LVGL-Standard ist ein duenner
     * Strich, auf diesem Geraet (seniorengerecht grosse Bedienelemente)
     * kaum als Bildlaufleiste erkennbar. */
    lv_obj_set_style_bg_color(inhalt, lv_color_hex(FARBE_SCHIEBER_EIN), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(inhalt, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(inhalt, 14, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(inhalt, 7, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(inhalt, 4, LV_PART_SCROLLBAR);

    /* Verfuegbare Textbreite je Zeile aus der TATSAECHLICHEN Inhaltsbreite
     * ableiten (nicht aus FENSTER_BREITE): links Rand 20, rechts die Checkbox
     * samt ihrem Rand, dazwischen etwas Luft. Laengere Texte werden damit
     * abgeschnitten statt umgebrochen und koennen die Checkbox nie
     * verschieben (FALLSTRICKE #19/#22). */
    lv_obj_update_layout(inhalt);
    int32_t text_breite = lv_obj_get_content_width(inhalt) - 20 - CHECKBOX_RAND - CHECKBOX_GROESSE - 12;

    /* Zeilen buendig bei 0 beginnen - ein Startversatz liesse die letzte der
     * HEUTE_INHALT_SICHTBAR_HOEHE-Zeilen genau um diesen Betrag angeschnitten
     * enden (live beobachtet: 8px Versatz schnitt die dritte Checkbox unten ab). */
    int32_t y = 0;
    bool tablette_vorhanden = false;
    for (int i = 0; i < anzahl; i++) {
        if (!eintraege[i].ist_tablette)
            continue;
        tablette_vorhanden = true;

        /* Kein "[x] "-Praefix mehr noetig - die Checkbox rechts zeigt den
         * (vorlaeufigen) Zustand, siehe tablette_checkbox_erzeugen. */
        char inhalt_zeile[ZEILE_MAX];
        eintrag_zeile_formatieren(&eintraege[i], false, inhalt_zeile, sizeof inhalt_zeile);

        uint32_t farbe;
        switch (kalender_tablette_status(&eintraege[i], true, jetzt_minuten)) {
        case KALENDER_TABLETTE_ABGEHAKT:     farbe = FARBE_VERGANGEN; break;
        case KALENDER_TABLETTE_FAELLIG:      farbe = FARBE_TABLETTE_FAELLIG; break;
        case KALENDER_TABLETTE_UEBERFAELLIG: farbe = FARBE_TABLETTE_UEBERFAELLIG; break;
        default:                             farbe = FARBE_FENSTER_TEXT; break;
        }

        /* Notiz aus dem Kalender (ICS-DESCRIPTION, z. B. "nuechtern") unter
         * dem Namen, gedaempft - wie im Erinnerungsfenster. Ohne Notiz steht
         * der Name allein und mittig in der Zeile. */
        bool hat_notiz = eintraege[i].beschreibung[0] != '\0';

        lv_obj_t *label = lv_label_create(inhalt);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_size(label, text_breite, 32);
        lv_obj_set_pos(label, 20, y + (hat_notiz ? 4 : 18));
        lv_obj_set_style_text_font(label, &schrift_klein_28, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(farbe), 0);
        lv_label_set_text(label, inhalt_zeile);

        if (hat_notiz) {
            lv_obj_t *notiz = lv_label_create(inhalt);
            lv_label_set_long_mode(notiz, LV_LABEL_LONG_DOT);
            lv_obj_set_size(notiz, text_breite, 30);
            lv_obj_set_pos(notiz, 20, y + 36);
            lv_obj_set_style_text_font(notiz, &schrift_klein_28, 0);
            lv_obj_set_style_text_color(notiz, lv_color_hex(FARBE_VERGANGEN), 0);
            lv_label_set_text(notiz, eintraege[i].beschreibung);
        }

        tablette_checkbox_erzeugen(inhalt, y + (HEUTE_ZEILE_TABLETTE_HOEHE - CHECKBOX_GROESSE) / 2, i, eintraege[i].bestaetigt);

        y += HEUTE_ZEILE_TABLETTE_HOEHE;
    }
    if (!tablette_vorhanden) {
        lv_obj_t *label = lv_label_create(inhalt);
        lv_label_set_text(label, "Keine Tabletten heute.");
        lv_obj_set_style_text_font(label, &schrift_klein_28, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(FARBE_FENSTER_TEXT), 0);
        lv_obj_set_pos(label, 20, y + 8);
        y += HEUTE_LEER_HINWEIS_HOEHE;
    }

    bool termin_ueberschrift_da = false;
    for (int i = 0; i < anzahl; i++) {
        if (eintraege[i].ist_tablette)
            continue;
        if (!termin_ueberschrift_da) {
            lv_obj_t *ueberschrift = lv_label_create(inhalt);
            lv_label_set_text(ueberschrift, "TERMINE");
            lv_obj_set_style_text_font(ueberschrift, &schrift_klein_28, 0);
            lv_obj_set_style_text_color(ueberschrift, lv_color_hex(FARBE_FENSTER_AKZENT), 0);
            lv_obj_set_pos(ueberschrift, 20, y + 8);
            y += HEUTE_TERMINE_KOPF_HOEHE;
            termin_ueberschrift_da = true;
        }
        char inhalt_zeile[ZEILE_MAX];
        eintrag_zeile_formatieren(&eintraege[i], false, inhalt_zeile, sizeof inhalt_zeile);
        bool vergangen = !eintraege[i].ganztags &&
                          (eintraege[i].stunde * 60 + eintraege[i].minute) < jetzt_minuten;
        lv_obj_t *label = lv_label_create(inhalt);
        lv_label_set_text(label, inhalt_zeile);
        lv_obj_set_style_text_font(label, &schrift_klein_28, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(vergangen ? FARBE_VERGANGEN : FARBE_FENSTER_TEXT), 0);
        if (vergangen)
            lv_obj_set_style_text_decor(label, LV_TEXT_DECOR_STRIKETHROUGH, 0);
        lv_obj_set_pos(label, 20, y + 8);
        y += HEUTE_ZEILE_TERMIN_HOEHE;
    }

    if (tablette_vorhanden)
        ok_abbrechen_erzeugen(s_heute_fenster, heute_ok_cb, heutefenster_schliessen_cb);

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

/* Sammelt die Indizes aller heutigen, unbestaetigten Tabletten, die gerade
 * FAELLIG oder UEBERFAELLIG sind (kalender_tablette_status) - genau die
 * Menge, die die Erinnerungs-Checkliste anzeigt. Rueckgabe: Anzahl. */
static int faellige_tabletten_sammeln(const kalender_tag_eintrag_t *eintraege, int anzahl,
                                       int jetzt_minuten, int *indizes_aus, int max)
{
    int n = 0;
    for (int i = 0; i < anzahl && n < max; i++) {
        if (!eintraege[i].ist_tablette)
            continue;
        kalender_tablette_status_t status = kalender_tablette_status(&eintraege[i], true, jetzt_minuten);
        if (status == KALENDER_TABLETTE_FAELLIG || status == KALENDER_TABLETTE_UEBERFAELLIG)
            indizes_aus[n++] = i;
    }
    return n;
}

static void erinnerung_ok_cb(lv_event_t *e)
{
    (void)e;
    checkboxen_uebernehmen();
    lvgl_port_lock(0);
    erinnerung_intern_schliessen();
    lvgl_port_unlock();
}

/* Zeigt eine Checkliste aller gerade faelligen/ueberfaelligen, unbestaetigten
 * Tabletten - sammelt sie selbst (siehe faellige_tabletten_sammeln), daher
 * ohne Index-Parameter. Tut nichts, wenn es aktuell keine gibt (z. B. wenn
 * app_main.c/erinnerung_pruefen zwischenzeitlich veraltete Daten hatte) oder
 * die Uhrzeit nicht bekannt ist. */
void tagesansicht_erinnerung_zeigen(void)
{
    if (!zeit_ist_synchron())
        return;

    kalender_tag_eintrag_t eintraege[KALENDER_EINTRAEGE_MAX];
    int anzahl = kalender_anzeige_heutige_eintraege(eintraege, KALENDER_EINTRAEGE_MAX);

    time_t jetzt = time(NULL);
    struct tm lokal;
    localtime_r(&jetzt, &lokal);
    int jetzt_minuten = lokal.tm_hour * 60 + lokal.tm_min;

    int indizes[KALENDER_EINTRAEGE_MAX];
    int n = faellige_tabletten_sammeln(eintraege, anzahl, jetzt_minuten, indizes, KALENDER_EINTRAEGE_MAX);
    if (n == 0)
        return;

    lvgl_port_lock(0);
    erinnerung_intern_schliessen();
    heutefenster_intern_schliessen(); /* nur ein Fenster gleichzeitig */
    tagesfenster_intern_schliessen();

    s_erinnerung_fenster = fenster_grundgeruest_erzeugen(n == 1 ? "TABLETTE NEHMEN" : "TABLETTEN NEHMEN",
                                                          "Bitte bestaetigen",
                                                          FENSTER_HOEHE_ERINNERUNG, erinnerung_schliessen_cb, false,
                                                          X_BUTTON_RAND_STANDARD);

    int32_t y = 100;
    int gezeigt = 0;
    for (int k = 0; k < n; k++) {
        if (gezeigt >= ERINNERUNG_ZEILEN_MAX - (n > ERINNERUNG_ZEILEN_MAX ? 1 : 0))
            break;
        int i = indizes[k];
        int32_t breite = FENSTER_BREITE - 40 - CHECKBOX_GROESSE - 20;

        /* Name + Uhrzeit ueber die vorhandene Formatierfunktion - dieselbe
         * "HH:MM  Titel"-Zeile wie ueberall sonst in diesem Modul.
         * Abschneiden statt umbrechen (FALLSTRICKE #22/#19): ein langer
         * Name darf die Checkbox nicht verschieben. */
        char inhalt[ZEILE_MAX];
        eintrag_zeile_formatieren(&eintraege[i], false, inhalt, sizeof inhalt);

        lv_obj_t *name = lv_label_create(s_erinnerung_fenster);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_size(name, breite, 32);
        lv_obj_set_pos(name, 20, y);
        lv_obj_set_style_text_font(name, &schrift_klein_28, 0);
        lv_obj_set_style_text_color(name, lv_color_hex(FARBE_FENSTER_AKZENT), 0);
        lv_label_set_text(name, inhalt);

        if (eintraege[i].beschreibung[0]) {
            lv_obj_t *beschreibung = lv_label_create(s_erinnerung_fenster);
            lv_label_set_long_mode(beschreibung, LV_LABEL_LONG_DOT);
            lv_obj_set_size(beschreibung, breite, 30);
            lv_obj_set_pos(beschreibung, 20, y + 32);
            lv_obj_set_style_text_font(beschreibung, &schrift_klein_28, 0);
            lv_obj_set_style_text_color(beschreibung, lv_color_hex(FARBE_VERGANGEN), 0);
            lv_label_set_text(beschreibung, eintraege[i].beschreibung);
        }

        tablette_checkbox_erzeugen(s_erinnerung_fenster, y + (ERINNERUNG_ZEILE_HOEHE - CHECKBOX_GROESSE) / 2,
                                    i, eintraege[i].bestaetigt);

        y += ERINNERUNG_ZEILE_HOEHE;
        gezeigt++;
    }
    if (n > gezeigt) {
        lv_obj_t *mehr = lv_label_create(s_erinnerung_fenster);
        char text[24];
        snprintf(text, sizeof text, "+%d weitere", n - gezeigt);
        lv_label_set_text(mehr, text);
        lv_obj_set_style_text_font(mehr, &schrift_klein_28, 0);
        lv_obj_set_style_text_color(mehr, lv_color_hex(FARBE_VERGANGEN), 0);
        lv_obj_set_pos(mehr, 20, y);
    }

    ok_abbrechen_erzeugen(s_erinnerung_fenster, erinnerung_ok_cb, erinnerung_schliessen_cb);

    s_erinnerung_fenster_timer = lv_timer_create(erinnerung_timer_cb, ERINNERUNG_ANZEIGEDAUER_MS, NULL);
    lv_timer_set_repeat_count(s_erinnerung_fenster_timer, 1);
    lvgl_port_unlock();
}

/* ---- Update-Hinweisfenster (waehrend eines laufenden OTA-Downloads) --- */

#define FENSTER_HOEHE_UPDATE 200

static lv_obj_t *s_update_fenster;
static lv_obj_t *s_update_balken;
static lv_obj_t *s_update_prozent_label;

/* Das "X" schliesst hier NUR die Benachrichtigung - der Download laeuft im
 * Hintergrund unbeeinflusst weiter und das Geraet startet am Ende trotzdem
 * neu (siehe ota.c). Genau wie bei den anderen Fenstern dieses Moduls
 * bewusst kein Abbrechen ueber die UI, nur ein Wegklicken der Anzeige. */
static void update_fenster_intern_schliessen(void)
{
    if (s_update_fenster) {
        lv_obj_delete(s_update_fenster);
        s_update_fenster = NULL;
        s_update_balken = NULL;
        s_update_prozent_label = NULL;
    }
}

static void update_fenster_schliessen_cb(lv_event_t *e)
{
    (void)e;
    lvgl_port_lock(0);
    update_fenster_intern_schliessen();
    lvgl_port_unlock();
}

/* Einmalig aufzurufen, sobald ota_laeuft() auf true wechselt (siehe
 * app_main.c/uhr_tick) - ruhiges Hinweisfenster, damit die Eltern den
 * anschliessenden Neustart einordnen koennen. Schliesst dabei die anderen
 * Overlays dieses Moduls, damit sich nichts ueberlappt. */
void tagesansicht_update_fenster_zeigen(void)
{
    lvgl_port_lock(0);
    if (s_update_fenster) {
        lvgl_port_unlock();
        return;
    }
    tagesfenster_intern_schliessen();
    heutefenster_intern_schliessen();
    erinnerung_intern_schliessen();

    s_update_fenster = fenster_grundgeruest_erzeugen("AKTUALISIERUNG",
                                                       "Bitte kurz warten - das Geraet startet danach neu",
                                                       FENSTER_HOEHE_UPDATE, update_fenster_schliessen_cb, false,
                                                       X_BUTTON_RAND_STANDARD);

    s_update_balken = lv_bar_create(s_update_fenster);
    lv_obj_set_size(s_update_balken, FENSTER_BREITE - 40, 28);
    lv_obj_set_pos(s_update_balken, 20, 110);
    lv_bar_set_range(s_update_balken, 0, 100);
    lv_bar_set_value(s_update_balken, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_update_balken, lv_color_hex(FARBE_SCHIEBER_AUS), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_update_balken, lv_color_hex(FARBE_FENSTER_AKZENT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_update_balken, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(s_update_balken, 8, LV_PART_INDICATOR);

    s_update_prozent_label = lv_label_create(s_update_fenster);
    lv_label_set_text(s_update_prozent_label, "Laedt...");
    lv_obj_set_style_text_font(s_update_prozent_label, &schrift_klein_28, 0);
    lv_obj_set_style_text_color(s_update_prozent_label, lv_color_hex(FARBE_FENSTER_TEXT), 0);
    lv_obj_set_pos(s_update_prozent_label, 20, 150);

    lvgl_port_unlock();
}

/* Gefahrlos jeden Tick aufrufbar, auch wenn der Benutzer das Fenster per
 * "X" bereits weggeklickt hat (dann ein No-Op). prozent < 0 bedeutet
 * "Groesse unbekannt" (siehe ota_fortschritt_prozent). */
void tagesansicht_update_fenster_fortschritt_setzen(int prozent)
{
    lvgl_port_lock(0);
    if (s_update_fenster) {
        if (prozent >= 0) {
            lv_bar_set_value(s_update_balken, prozent, LV_ANIM_ON);
            char text[16]; /* grosszuegig, damit GCCs Format-Truncation-Pruefung
                             * nicht am theoretischen int-Wertebereich anstoesst -
                             * derselbe Fallstrick wie bei den Uebersichtszeilen. */
            snprintf(text, sizeof text, "%d %%", prozent);
            lv_label_set_text(s_update_prozent_label, text);
        } else {
            lv_label_set_text(s_update_prozent_label, "Laedt...");
        }
    }
    lvgl_port_unlock();
}

/* Aufzurufen, sobald ota_laeuft() wieder auf false wechselt (Update fertig
 * oder fehlgeschlagen) - bei Erfolg startet das Geraet ohnehin sofort neu,
 * dies deckt vor allem den Fehlerfall ab (Fenster soll nicht ewig auf
 * "Laedt..." stehen bleiben). */
void tagesansicht_update_fenster_schliessen(void)
{
    lvgl_port_lock(0);
    update_fenster_intern_schliessen();
    lvgl_port_unlock();
}

bool tagesansicht_fenster_offen(void)
{
    return s_tages_fenster != NULL || s_heute_fenster != NULL ||
           s_erinnerung_fenster != NULL;
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
