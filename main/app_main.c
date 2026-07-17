/*
 * app_main.c — Seniorenuhr, Einstieg.
 *
 * Phase 1: WLAN + NTP, echte laufende Uhr mit deutschem Wochentag/Datum.
 * Phase 4 (vorgezogen): Termine/Tabletten aus dem Google-Kalender.
 * Phase 2: Tag/Abend/Nacht-Farbschema, Beruehrung weckt kurz auf.
 * Solange keine Zeit/keine Kalenderdaten bekannt sind, wird das offen
 * angezeigt statt einer falschen Uhrzeit oder leerer Listen (siehe
 * FAHRPLAN.md).
 */
#include <string.h>
#include <time.h>

#include "anzeige.h"
#include "einrichtung.h"
#include "einstellungen.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "kalender_anzeige.h"
#include "netz.h"
#include "startbildschirm.h"
#include "tagesansicht.h"
#include "zeit.h"

#define BERUEHRUNG_WACHZEIT_US (30LL * 1000000)

/* Mindestabstand zwischen zwei Deckkraft-Aenderungen waehrend der Einblend-
 * Animation. Ohne diese Bremse ruft lv_anim bei jedem LVGL-Tick (ca. alle
 * 30ms) lv_obj_set_style_bg_opa auf dem bildschirmfuellenden Overlay auf -
 * jeder dieser ~60 Aufrufe in 2s erzwingt ein komplettes Neuzeichnen des
 * ganzen Panels und verschaerft die PSRAM-Bus-Kontention mit dem WLAN
 * (siehe FALLSTRICKE_UND_WORKAROUNDS.md #6) zu sichtbarem Flackern. */
#define EINBLEND_MIND_ABSTAND_US (100 * 1000)

/* Farben */
#define FARBE_TAG_HINTERGRUND   0x123a63
#define FARBE_NACHT_HINTERGRUND 0x000000
#define FARBE_AKZENT            0xffd75f /* Wochentag/Ueberschriften am Tag */
#define FARBE_TEXT_HELL         0xffffff /* Uhrzeit/Listen am Tag */
#define FARBE_STATUS_HELL       0xd0e0f0 /* Tageszeit/Datum am Tag */
#define FARBE_NACHT_TEXT        0x1d1d1d /* alles Text nachts: dunkles Grau, halbe Helligkeit */
#define FARBE_WARNUNG           0xff5a4a /* Durchstrich der Status-Symbole bei fehlender Konnektivitaet */
#define FARBE_ICON_HELLGRAU     0xd8d8d8 /* Status-Symbole rechts oben - fest, unabhaengig vom Modus */
#define FARBE_VERGANGEN         0x707a8a /* gedaempftes Grau: vergangene Termine / abgehakte Tabletten in der Uebersicht */

/* Kantenlaenge der kleinen Status-Symbole rechts oben (WLAN/Zeit/Kalender). */
#define STATUS_ICON_GROESSE 34
#define STATUS_ICON_MAX_TEILE 4

LV_FONT_DECLARE(schrift_uhr_128);
LV_FONT_DECLARE(schrift_gross_72);
LV_FONT_DECLARE(schrift_mittel_40);
LV_FONT_DECLARE(schrift_klein_28);

static const char *TAG = "seniorenuhr";

/* Deutscher Klartext fuer esp_reset_reason() - wird ganz am Anfang von
 * app_main() geloggt, damit ein unerwarteter Neustart wenigstens beim
 * NAECHSTEN Boot im (dann laufenden) seriellen Log erkennbar ist. Ohne
 * dauerhaft mitlaufenden Monitor gibt es sonst keine Spur, WARUM ein
 * Neustart passiert ist (WLAN-Watchdog, Absturz, Stromausfall, ...). */
static const char *reset_grund_text(esp_reset_reason_t grund)
{
    switch (grund) {
    case ESP_RST_POWERON:   return "Stromversorgung eingeschaltet";
    case ESP_RST_EXT:       return "externer Reset-Pin";
    case ESP_RST_SW:        return "Software (esp_restart, z. B. WLAN-Watchdog oder Speichern im Einrichtungs-Bildschirm)";
    case ESP_RST_PANIC:     return "Absturz/Exception (Panic)";
    case ESP_RST_INT_WDT:   return "interner Watchdog (haengender Interrupt)";
    case ESP_RST_TASK_WDT:  return "Task-Watchdog (haengender Task)";
    case ESP_RST_WDT:       return "sonstiger Watchdog";
    case ESP_RST_DEEPSLEEP: return "Aufwachen aus Deep-Sleep (wird hier nicht genutzt)";
    case ESP_RST_BROWNOUT:  return "Unterspannung (Brownout, z. B. schwaches Netzteil)";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "unbekannt";
    }
}

typedef enum { MODUS_TAG, MODUS_ABEND, MODUS_NACHT } anzeige_modus_t;

static lv_obj_t *s_bildschirm;
static lv_obj_t *s_wochentag_label;
static lv_obj_t *s_uhr_label;
static lv_obj_t *s_status_label;
static lv_obj_t *s_tabletten_ueberschrift;
static lv_obj_t *s_termine_ueberschrift;

/* Tabletten/Termine-Uebersicht: pro Eintrag ein eigenes Label statt eines
 * einzigen mehrzeiligen Textblocks - noetig, damit vergangene Termine
 * durchgestrichen werden koennen (LVGLs Recolor-Markup faerbt nur, eine
 * Text-Dekoration wie Durchstreichen laesst sich ausschliesslich pro
 * Objekt setzen, nicht pro Zeile innerhalb eines Labels). Alle Zeilen
 * haengen als Kinder an einem gemeinsamen, unsichtbaren Container - der
 * uebernimmt Ein-/Ausblenden fuer den Abend-/Nacht-Modus (siehe
 * modus_anwenden), ohne dass dafuer jede einzelne Zeile einzeln behandelt
 * werden muesste. */
#define UEBERSICHT_ZEILEN_MAX KALENDER_EINTRAEGE_MAX
#define UEBERSICHT_ZEILE_ABSTAND 34
#define UEBERSICHT_SPALTE_BREITE 300
/* Abgehakte Tabletten bekommen dieses ASCII-Praefix statt eines Unicode-
 * Hakens - Montserrat-Bold (unsere generierte Schriftart) enthaelt keine
 * Symbolglyphen wie U+2713, lv_font_conv bricht dafuer mit "doesn't have
 * any characters included in range" ab (siehe tools/fonts/erzeuge_fonts.ps1). */
#define UEBERSICHT_HAKEN_PRAEFIX "[x] "

typedef struct {
    lv_obj_t *container;
    lv_obj_t *zeilen[UEBERSICHT_ZEILEN_MAX];
    int anzahl;
} uebersicht_spalte_t;

static uebersicht_spalte_t s_tabletten_spalte;
static uebersicht_spalte_t s_termine_spalte;
static lv_obj_t *s_dimm_overlay; /* nur fuer den Abend-Modus verwendet */

/* Bei Beruehrung waehrend Abend/Nacht wechselt die Anzeige fuer diese
 * Wachzeit vollstaendig in den Tag-Modus (siehe beruehrung_callback). */
static volatile int64_t s_wach_bis_us = 0;

static void beruehrung_callback(lv_event_t *e)
{
    (void)e;
    s_wach_bis_us = esp_timer_get_time() + BERUEHRUNG_WACHZEIT_US;
}

/* Tipp auf die Tabletten/Termine-Uebersicht oeffnet direkt das "Heute"-
 * Fenster - bisher ging das nur ueber den eigenen "Heute"-Button links. */
static void uebersicht_geklickt_cb(lv_event_t *e)
{
    (void)e;
    tagesansicht_heute_oeffnen();
}

/* Macht ein Label per Tipp klickbar (Ueberschrift/Inhalt der Tabletten-
 * bzw. Termine-Uebersicht) - Labels sind in LVGL standardmaessig nicht
 * klickbar. */
static void uebersicht_tippbar_machen(lv_obj_t *label)
{
    lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(label, uebersicht_geklickt_cb, LV_EVENT_CLICKED, NULL);
}

/* lv_obj_set_style_bg_opa hat 3 Parameter (Objekt, Deckkraft, Selektor) -
 * lv_anim braucht aber genau 2 (Objekt, Wert). Ohne diesen kleinen
 * Wrapper wuerde ein direkter Function-Pointer-Cast den Selektor-
 * Parameter mit Zufallswerten vom Stack befuellen. */
static void einblend_anim_cb(void *var, int32_t wert)
{
    static int64_t s_letzte_zeit_us = 0;
    int64_t jetzt_us = esp_timer_get_time();
    if (jetzt_us - s_letzte_zeit_us < EINBLEND_MIND_ABSTAND_US)
        return;
    s_letzte_zeit_us = jetzt_us;
    lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)wert, 0);
}

/* Deckkraft des Dimm-Overlays, die zu diesem Modus gehoert (0 = unsichtbar). */
static lv_opa_t overlay_ziel_fuer_modus(anzeige_modus_t modus)
{
    return (modus == MODUS_ABEND) ? LV_OPA_70 : LV_OPA_TRANSP;
}

/* Ermittelt den aktuellen Anzeigemodus aus Uhrzeit + Beruehrungs-Aufweckzeit.
 * Ohne bekannte Uhrzeit gibt es nichts abzudunkeln -> Tag-Modus. Haengt
 * bewusst nur an zeit_ist_synchron() (nicht zusaetzlich an WLAN) - im
 * Offline-Betrieb (siehe einrichtung.c) ist die Zeit manuell gesetzt und
 * damit gueltig, obwohl kein Netz besteht. */
static anzeige_modus_t aktueller_modus(void)
{
    if (!zeit_ist_synchron())
        return MODUS_TAG;

    /* Waehrend ein Tages-/Heute-Fenster offen ist, bleibt es Tag-Modus -
     * Presses auf Elemente innerhalb dieser Fenster (Schieberegler,
     * Schliessen-Button, ...) bubbeln nicht bis zu beruehrung_callback
     * durch und wuerden die 30s-Wachzeit sonst nicht verlaengern, obwohl
     * der Benutzer aktiv am Geraet ist (z. B. beim Abhaken mehrerer
     * Tabletten). */
    if (tagesansicht_fenster_offen())
        return MODUS_TAG;

    time_t jetzt = time(NULL);
    struct tm lokal;
    localtime_r(&jetzt, &lokal);
    const char *tageszeit = zeit_tageszeit(&lokal);

    anzeige_modus_t modus = MODUS_TAG;
    if (strcmp(tageszeit, "Nacht") == 0)
        modus = MODUS_NACHT;
    else if (strcmp(tageszeit, "Abend") == 0)
        modus = MODUS_ABEND;

    if (modus != MODUS_TAG && esp_timer_get_time() < s_wach_bis_us)
        modus = MODUS_TAG;
    return modus;
}

/* Ueberschrift mit fester Breite + Zeilenumbruch statt freiem Auto-Wachstum —
 * sonst kann eine laengere Ueberschrift in die Nachbarspalte hineinlaufen. */
static lv_obj_t *ueberschrift_erzeugen(lv_obj_t *scr, const char *text, int32_t x, int32_t y, int32_t breite)
{
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &schrift_klein_28, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(FARBE_AKZENT), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, breite);
    lv_obj_set_pos(label, x, y);
    return label;
}

/* Unsichtbarer Container fuer die Zeilen-Labels einer Spalte - siehe
 * uebersicht_spalte_t weiter oben. */
static lv_obj_t *uebersicht_container_erzeugen(lv_obj_t *scr, int32_t x, int32_t y, int32_t breite)
{
    lv_obj_t *cont = lv_obj_create(scr);
    lv_obj_remove_style_all(cont);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(cont, x, y);
    lv_obj_set_size(cont, breite, LV_SIZE_CONTENT);
    return cont;
}

/* Baut eine Fingerabdruck-Zeichenkette fuer eine der beiden Spalten
 * (Tabletten/Termine) aus den strukturierten Tageseintraegen - dient NUR
 * dem billigen Aenderungs-Check in uhr_tick() (strcmp gegen den vorherigen
 * Aufruf), nicht der direkten Anzeige. Die Faerbung wird als Farbcode mit
 * eincodiert, damit sich auch ein reiner Bestaetigt-/Vergangen-Wechsel
 * (ohne Textaenderung) zuverlaessig im Fingerabdruck niederschlaegt. */
static void liste_text_aufbauen(const kalender_tag_eintrag_t *eintraege, int anzahl, bool nur_tabletten,
                                bool zeit_bekannt, int jetzt_minuten, char *ziel, size_t ziel_groesse)
{
    ziel[0] = '\0';
    size_t belegt = 0;
    int gefunden = 0;

    for (int i = 0; i < anzahl; i++) {
        if (eintraege[i].ist_tablette != nur_tabletten)
            continue;
        gefunden++;

        char inhalt[80];
        if (eintraege[i].ganztags)
            snprintf(inhalt, sizeof inhalt, "%s", eintraege[i].titel);
        else
            snprintf(inhalt, sizeof inhalt, "%02d:%02d  %s",
                     eintraege[i].stunde, eintraege[i].minute, eintraege[i].titel);

        bool gedaempft = nur_tabletten
            ? eintraege[i].bestaetigt
            : (zeit_bekannt && !eintraege[i].ganztags &&
               (eintraege[i].stunde * 60 + eintraege[i].minute) < jetzt_minuten);

        char zeile[104];
        if (gedaempft)
            snprintf(zeile, sizeof zeile, "#%06x %s#\n", FARBE_VERGANGEN, inhalt);
        else
            snprintf(zeile, sizeof zeile, "%s\n", inhalt);

        size_t n = strlen(zeile);
        if (belegt + n < ziel_groesse) {
            memcpy(ziel + belegt, zeile, n);
            belegt += n;
            ziel[belegt] = '\0';
        }
    }
    if (gefunden == 0)
        snprintf(ziel, ziel_groesse, "-");
}

static void uebersicht_spalte_leeren(uebersicht_spalte_t *spalte)
{
    for (int i = 0; i < spalte->anzahl; i++)
        lv_obj_delete(spalte->zeilen[i]);
    spalte->anzahl = 0;
}

/* Zeigt einen einzelnen Platzhaltertext an (z. B. "..." bevor Kalenderdaten
 * bekannt sind) - ersetzt alle bisherigen Zeilen. */
static void uebersicht_spalte_platzhalter_setzen(uebersicht_spalte_t *spalte, const char *text)
{
    uebersicht_spalte_leeren(spalte);
    lv_obj_t *label = lv_label_create(spalte->container);
    lv_obj_set_style_text_font(label, &schrift_klein_28, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(FARBE_TEXT_HELL), 0);
    lv_obj_set_pos(label, 0, 0);
    lv_label_set_text(label, text);
    uebersicht_tippbar_machen(label);
    spalte->zeilen[spalte->anzahl++] = label;
}

/* Loescht alle aktuellen Zeilen-Labels einer Spalte und baut sie aus den
 * strukturierten Tageseintraegen neu auf - wird nur aufgerufen, wenn sich
 * der Fingerabdruck (liste_text_aufbauen) tatsaechlich geaendert hat.
 * Abgehakte Tabletten bekommen das "[x] "-Praefix (siehe
 * UEBERSICHT_HAKEN_PRAEFIX) plus gedaempfte Farbe; vergangene Termine
 * werden zusaetzlich durchgestrichen (Peters ausdruecklicher Wunsch nach
 * dem Demo-Test: Tabletten nur abgehakt, Termine durchgestrichen). */
static void uebersicht_spalte_neu_aufbauen(uebersicht_spalte_t *spalte, int32_t breite,
                                            const kalender_tag_eintrag_t *eintraege, int anzahl,
                                            bool nur_tabletten, bool zeit_bekannt, int jetzt_minuten)
{
    uebersicht_spalte_leeren(spalte);

    int32_t y = 0;
    int gefunden = 0;
    for (int i = 0; i < anzahl && spalte->anzahl < UEBERSICHT_ZEILEN_MAX; i++) {
        if (eintraege[i].ist_tablette != nur_tabletten)
            continue;
        gefunden++;

        bool abgehakt = nur_tabletten && eintraege[i].bestaetigt;
        bool vergangen = !nur_tabletten && zeit_bekannt && !eintraege[i].ganztags &&
                          (eintraege[i].stunde * 60 + eintraege[i].minute) < jetzt_minuten;
        bool gedaempft = abgehakt || vergangen;

        char inhalt[88];
        const char *praefix = abgehakt ? UEBERSICHT_HAKEN_PRAEFIX : "";
        if (eintraege[i].ganztags)
            snprintf(inhalt, sizeof inhalt, "%s%s", praefix, eintraege[i].titel);
        else
            snprintf(inhalt, sizeof inhalt, "%s%02d:%02d  %s", praefix,
                     eintraege[i].stunde, eintraege[i].minute, eintraege[i].titel);

        lv_obj_t *label = lv_label_create(spalte->container);
        lv_obj_set_style_text_font(label, &schrift_klein_28, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(gedaempft ? FARBE_VERGANGEN : FARBE_TEXT_HELL), 0);
        if (vergangen)
            lv_obj_set_style_text_decor(label, LV_TEXT_DECOR_STRIKETHROUGH, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label, breite);
        lv_obj_set_pos(label, 0, y);
        lv_label_set_text(label, inhalt);
        uebersicht_tippbar_machen(label);

        spalte->zeilen[spalte->anzahl++] = label;
        y += UEBERSICHT_ZEILE_ABSTAND;
    }

    if (gefunden == 0 && spalte->anzahl < UEBERSICHT_ZEILEN_MAX) {
        lv_obj_t *label = lv_label_create(spalte->container);
        lv_obj_set_style_text_font(label, &schrift_klein_28, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(FARBE_TEXT_HELL), 0);
        lv_obj_set_pos(label, 0, 0);
        lv_label_set_text(label, "-");
        uebersicht_tippbar_machen(label);
        spalte->zeilen[spalte->anzahl++] = label;
    }
}

/* Kleines Status-Symbol rechts oben: Kreisring mit einem Mini-Glyph
 * (WLAN-Balken/Uhr-Zeiger/Kalender-Kopf, siehe status_glyph_*_erzeugen)
 * darin - spiegelt dieselben drei Boot-Phasen aus startbildschirm.c.
 * Ohne Konnektivitaet erscheint ein diagonaler Durchstrich darueber.
 *
 * Ring und Glyph-Teile sind bewusst GESCHWISTER unter einem gemeinsamen,
 * unsichtbaren Container - nicht Ring als Elternteil der Glyphen. LVGL
 * rueckt den Innenbereich eines Objekts mit Rand (border_width) sonst
 * etwas ein, wodurch relativ zum Ring positionierte Kinder leicht nach
 * rechts unten verschoben erscheinen (= der Ring wirkt nach links oben
 * versetzt). Genau dasselbe Muster nutzt schon icon_uhr_erzeugen() in
 * startbildschirm.c (Zeiger als Geschwister des Kreises, nicht als
 * dessen Kinder). */
typedef struct {
    lv_obj_t *container;
    lv_obj_t *ring;
    lv_obj_t *glyph_teile[STATUS_ICON_MAX_TEILE];
    int glyph_anzahl;
    lv_obj_t *durchstrich;
} status_icon_t;

static status_icon_t s_status_wlan;
static status_icon_t s_status_zeit;
static status_icon_t s_status_kalender;

static void status_icon_teil_hinzufuegen(status_icon_t *icon, lv_obj_t *obj)
{
    if (icon->glyph_anzahl < STATUS_ICON_MAX_TEILE)
        icon->glyph_teile[icon->glyph_anzahl++] = obj;
}

static void status_icon_farbe_setzen(status_icon_t *icon, lv_color_t farbe)
{
    lv_obj_set_style_border_color(icon->ring, farbe, 0);
    for (int i = 0; i < icon->glyph_anzahl; i++) {
        lv_obj_set_style_bg_color(icon->glyph_teile[i], farbe, 0);
        lv_obj_set_style_border_color(icon->glyph_teile[i], farbe, 0);
        lv_obj_set_style_line_color(icon->glyph_teile[i], farbe, 0);
    }
}

/* Blendet den Durchstrich ein (keine Konnektivitaet) bzw. aus (alles ok). */
static void status_icon_ok_setzen(status_icon_t *icon, bool ok)
{
    lvgl_port_lock(0);
    if (ok)
        lv_obj_add_flag(icon->durchstrich, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_remove_flag(icon->durchstrich, LV_OBJ_FLAG_HIDDEN);
    lvgl_port_unlock();
}

static void status_icon_erzeugen(status_icon_t *icon, lv_obj_t *scr, int32_t x)
{
    icon->glyph_anzahl = 0;

    icon->container = lv_obj_create(scr);
    lv_obj_remove_style_all(icon->container);
    lv_obj_set_size(icon->container, STATUS_ICON_GROESSE, STATUS_ICON_GROESSE);
    lv_obj_set_pos(icon->container, x, 14);
    lv_obj_remove_flag(icon->container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(icon->container, LV_OBJ_FLAG_SCROLLABLE);

    icon->ring = lv_obj_create(icon->container);
    lv_obj_remove_style_all(icon->ring);
    lv_obj_set_size(icon->ring, STATUS_ICON_GROESSE, STATUS_ICON_GROESSE);
    lv_obj_set_pos(icon->ring, 0, 0);
    lv_obj_set_style_radius(icon->ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(icon->ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon->ring, 2, 0);
    lv_obj_remove_flag(icon->ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(icon->ring, LV_OBJ_FLAG_SCROLLABLE);
}

/* Muss nach den Glyph-Teilen aufgerufen werden, damit der Strich als
 * letztes (oberstes) Geschwister ueber dem Glyph liegt. */
static void status_icon_durchstrich_erzeugen(status_icon_t *icon)
{
    static const lv_point_precise_t punkte[2] = {{5, 5}, {STATUS_ICON_GROESSE - 5, STATUS_ICON_GROESSE - 5}};
    icon->durchstrich = lv_line_create(icon->container);
    lv_line_set_points(icon->durchstrich, punkte, 2);
    lv_obj_set_style_line_width(icon->durchstrich, 3, 0);
    lv_obj_set_style_line_color(icon->durchstrich, lv_color_hex(FARBE_WARNUNG), 0);
    lv_obj_set_style_line_rounded(icon->durchstrich, true, 0);
    lv_obj_add_flag(icon->durchstrich, LV_OBJ_FLAG_HIDDEN); /* Start: als "ok" angenommen */
}

/* WLAN-Symbol: drei Balken steigender Hoehe (verkleinerte Version des
 * Startbildschirm-Symbols, siehe startbildschirm.c). */
static void status_glyph_wlan_erzeugen(status_icon_t *icon)
{
    static const int hoehen[3] = {7, 11, 15};
    const int breite = 4, luecke = 3;
    const int gesamt_breite = 3 * breite + 2 * luecke;
    const int x0 = (STATUS_ICON_GROESSE - gesamt_breite) / 2;

    for (int i = 0; i < 3; i++) {
        lv_obj_t *balken = lv_obj_create(icon->container);
        lv_obj_remove_style_all(balken);
        lv_obj_set_size(balken, breite, hoehen[i]);
        lv_obj_set_style_bg_opa(balken, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(balken, 1, 0);
        lv_obj_set_pos(balken, x0 + i * (breite + luecke), STATUS_ICON_GROESSE - 8 - hoehen[i]);
        lv_obj_remove_flag(balken, LV_OBJ_FLAG_CLICKABLE);
        status_icon_teil_hinzufuegen(icon, balken);
    }
}

/* Uhr-Symbol: nur die beiden Zeiger (der Ring selbst dient als Zifferblatt). */
static void status_glyph_zeit_erzeugen(status_icon_t *icon)
{
    static const lv_point_precise_t minutenzeiger[2] = {{17, 17}, {17, 6}};
    lv_obj_t *minute = lv_line_create(icon->container);
    lv_line_set_points(minute, minutenzeiger, 2);
    lv_obj_set_style_line_width(minute, 2, 0);
    lv_obj_set_style_line_rounded(minute, true, 0);
    status_icon_teil_hinzufuegen(icon, minute);

    static const lv_point_precise_t stundenzeiger[2] = {{17, 17}, {24, 17}};
    lv_obj_t *stunde = lv_line_create(icon->container);
    lv_line_set_points(stunde, stundenzeiger, 2);
    lv_obj_set_style_line_width(stunde, 2, 0);
    lv_obj_set_style_line_rounded(stunde, true, 0);
    status_icon_teil_hinzufuegen(icon, stunde);
}

/* Kalender-Symbol: abgerundetes Rechteck + Kopfleiste. */
static void status_glyph_kalender_erzeugen(status_icon_t *icon)
{
    lv_obj_t *rahmen = lv_obj_create(icon->container);
    lv_obj_remove_style_all(rahmen);
    lv_obj_set_size(rahmen, 20, 16);
    lv_obj_set_pos(rahmen, 7, 9);
    lv_obj_set_style_radius(rahmen, 2, 0);
    lv_obj_set_style_bg_opa(rahmen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rahmen, 2, 0);
    status_icon_teil_hinzufuegen(icon, rahmen);

    lv_obj_t *kopf = lv_obj_create(icon->container);
    lv_obj_remove_style_all(kopf);
    lv_obj_set_size(kopf, 20, 5);
    lv_obj_set_pos(kopf, 7, 9);
    lv_obj_set_style_radius(kopf, 2, 0);
    lv_obj_set_style_bg_opa(kopf, LV_OPA_COVER, 0);
    status_icon_teil_hinzufuegen(icon, kopf);
}

static void ui_aufbauen(void)
{
    lvgl_port_lock(0);

    /* Eigener, noch nicht angezeigter Screen - waehrend die Hauptanzeige
     * hier im Hintergrund aufgebaut wird, zeigt der aktuell aktive Screen
     * den Startbildschirm (siehe app_main). */
    s_bildschirm = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_bildschirm, lv_color_hex(FARBE_TAG_HINTERGRUND), 0);
    /* lv_obj_create() ist standardmaessig scrollbar - ohne dies liesse sich
     * die Anzeige per Touch ein paar Pixel hoch-/runterschieben (elastischer
     * Rueckfedereffekt), obwohl der Inhalt exakt in den Bildschirm passt. */
    lv_obj_remove_flag(s_bildschirm, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_bildschirm, beruehrung_callback, LV_EVENT_PRESSED, NULL);

    s_wochentag_label = lv_label_create(s_bildschirm);
    lv_label_set_text(s_wochentag_label, "...");
    lv_obj_set_style_text_font(s_wochentag_label, &schrift_gross_72, 0);
    lv_obj_set_style_text_color(s_wochentag_label, lv_color_hex(FARBE_AKZENT), 0);
    lv_obj_align(s_wochentag_label, LV_ALIGN_TOP_MID, 0, 10);

    s_uhr_label = lv_label_create(s_bildschirm);
    lv_label_set_text(s_uhr_label, "--:--");
    lv_obj_set_style_text_font(s_uhr_label, &schrift_uhr_128, 0);
    lv_obj_set_style_text_color(s_uhr_label, lv_color_hex(FARBE_TEXT_HELL), 0);
    lv_obj_align(s_uhr_label, LV_ALIGN_TOP_MID, 0, 95);

    s_status_label = lv_label_create(s_bildschirm);
    lv_label_set_text(s_status_label, "Verbinde mit WLAN...");
    lv_obj_set_style_text_font(s_status_label, &schrift_mittel_40, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(FARBE_STATUS_HELL), 0);
    lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, 275);

    /* Untere Haelfte: links Tabletten, rechts Termine - nachts ausgeblendet.
     * Etwas schmaler/eingerueckt als urspruenglich (war 20..390/410..780),
     * damit links die Wochentag-Buttons und rechts der Heute-Button Platz
     * haben (siehe tagesansicht_erstellen unten). */
    s_tabletten_ueberschrift = ueberschrift_erzeugen(s_bildschirm, "TABLETTEN HEUTE", 80, 335, 300);
    s_tabletten_spalte.container = uebersicht_container_erzeugen(s_bildschirm, 80, 385, UEBERSICHT_SPALTE_BREITE);
    uebersicht_tippbar_machen(s_tabletten_ueberschrift);

    s_termine_ueberschrift = ueberschrift_erzeugen(s_bildschirm, "TERMINE HEUTE", 420, 335, 300);
    s_termine_spalte.container = uebersicht_container_erzeugen(s_bildschirm, 420, 385, UEBERSICHT_SPALTE_BREITE);
    uebersicht_tippbar_machen(s_termine_ueberschrift);

    /* Wochentag-Navigation: 7 Buttons links (gestern..+5 Tage) + Heute-
     * Button rechts, oeffnen Tages-/Heute-Fenster mit Terminen/Tabletten. */
    tagesansicht_erstellen(s_bildschirm);

    /* Live-Status rechts oben: spiegelt WLAN/Zeit/Kalender aus dem
     * Startbildschirm, durchgestrichen bei fehlender Konnektivitaet. */
    status_icon_erzeugen(&s_status_wlan, s_bildschirm, 650);
    status_glyph_wlan_erzeugen(&s_status_wlan);
    status_icon_durchstrich_erzeugen(&s_status_wlan);

    status_icon_erzeugen(&s_status_zeit, s_bildschirm, 700);
    status_glyph_zeit_erzeugen(&s_status_zeit);
    status_icon_durchstrich_erzeugen(&s_status_zeit);

    status_icon_erzeugen(&s_status_kalender, s_bildschirm, 750);
    status_glyph_kalender_erzeugen(&s_status_kalender);
    status_icon_durchstrich_erzeugen(&s_status_kalender);

    /* Abend-Dimmung: ein schwarzes Rechteck ueber allem anderen. Fuer
     * Nacht wird stattdessen direkt mit Hintergrund-/Textfarben
     * gearbeitet (siehe modus_anwenden) - das Overlay bleibt dort
     * transparent. Muss als letztes Kind angelegt werden, damit es
     * ganz oben liegt. */
    s_dimm_overlay = lv_obj_create(s_bildschirm);
    lv_obj_remove_style_all(s_dimm_overlay);
    lv_obj_set_size(s_dimm_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_dimm_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_dimm_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_dimm_overlay, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(s_dimm_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_dimm_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lvgl_port_unlock();
}

/* Setzt Hintergrund, Textfarben, Overlay und Sichtbarkeit der
 * Tabletten/Termine-Sektion passend zum Modus. Wird nur bei
 * tatsaechlichem Moduswechsel aufgerufen (siehe uhr_tick) - andernfalls
 * wuerde jeder Sekunden-Tick einen kompletten Panel-Redraw ausloesen. */
static void modus_anwenden(anzeige_modus_t modus)
{
    lvgl_port_lock(0);

    uint32_t hintergrund = (modus == MODUS_NACHT) ? FARBE_NACHT_HINTERGRUND : FARBE_TAG_HINTERGRUND;
    uint32_t textfarbe_hell = (modus == MODUS_NACHT) ? FARBE_NACHT_TEXT : FARBE_TEXT_HELL;
    uint32_t textfarbe_akzent = (modus == MODUS_NACHT) ? FARBE_NACHT_TEXT : FARBE_AKZENT;
    uint32_t textfarbe_status = (modus == MODUS_NACHT) ? FARBE_NACHT_TEXT : FARBE_STATUS_HELL;
    lv_opa_t overlay_deckkraft = overlay_ziel_fuer_modus(modus);
    /* Nachts wird nicht nur gedimmt, sondern alles Zusaetzliche komplett
     * ausgeblendet - betrifft Tabletten/Termine und die Status-Symbole. */
    bool details_sichtbar = (modus != MODUS_NACHT);

    lv_obj_set_style_bg_color(s_bildschirm, lv_color_hex(hintergrund), 0);
    lv_obj_set_style_text_color(s_wochentag_label, lv_color_hex(textfarbe_akzent), 0);
    lv_obj_set_style_text_color(s_uhr_label, lv_color_hex(textfarbe_hell), 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(textfarbe_status), 0);
    lv_obj_set_style_bg_opa(s_dimm_overlay, overlay_deckkraft, 0);

    status_icon_farbe_setzen(&s_status_wlan, lv_color_hex(FARBE_ICON_HELLGRAU));
    status_icon_farbe_setzen(&s_status_zeit, lv_color_hex(FARBE_ICON_HELLGRAU));
    status_icon_farbe_setzen(&s_status_kalender, lv_color_hex(FARBE_ICON_HELLGRAU));
    tagesansicht_sichtbarkeit_setzen(details_sichtbar);

    if (details_sichtbar) {
        lv_obj_remove_flag(s_tabletten_ueberschrift, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_tabletten_spalte.container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_termine_ueberschrift, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_termine_spalte.container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_status_wlan.container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_status_zeit.container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_status_kalender.container, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_tabletten_ueberschrift, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_tabletten_spalte.container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_termine_ueberschrift, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_termine_spalte.container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_status_wlan.container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_status_zeit.container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_status_kalender.container, LV_OBJ_FLAG_HIDDEN);
    }

    lvgl_port_unlock();
}

/* Wird jede Sekunde von LVGL aufgerufen (siehe app_main). */
static void uhr_tick(lv_timer_t *timer)
{
    (void)timer;

    char uhrzeit[8];
    char datum[32];
    char status_text[48];
    const char *wochentag;
    const char *status;
    anzeige_modus_t modus = aktueller_modus();

    if (zeit_ist_synchron()) {
        time_t jetzt = time(NULL);
        struct tm lokal;
        localtime_r(&jetzt, &lokal);
        einstellungen_letzte_anzeige_setzen(jetzt);

        snprintf(uhrzeit, sizeof uhrzeit, "%02d:%02d", lokal.tm_hour, lokal.tm_min);
        wochentag = zeit_wochentag_gross(&lokal);
        zeit_datum_text(&lokal, datum, sizeof datum);

        const char *tageszeit = zeit_tageszeit(&lokal);
        snprintf(status_text, sizeof status_text, "%s, %s", tageszeit, datum);
        status = status_text;
    } else {
        snprintf(uhrzeit, sizeof uhrzeit, "--:--");
        wochentag = "...";
        status = netz_ist_verbunden() ? "Uhrzeit wird geholt..." : "Warte auf WLAN...";
    }

    /* Jedes Label nur bei tatsaechlicher Textaenderung neu setzen -
     * lv_label_set_text loest sonst jede Sekunde ein Redraw aus, auch
     * wenn sich nichts geaendert hat (Ursache des Flackerns beim
     * Minutenwechsel, wenn mehrere Labels gleichzeitig "unnoetig"
     * aktualisiert wuerden). */
    static char letzte_uhrzeit[sizeof uhrzeit] = "";
    static const char *letzter_wochentag = NULL;
    static char letzter_status[sizeof status_text] = "";

    bool uhrzeit_geaendert = strcmp(uhrzeit, letzte_uhrzeit) != 0;
    bool wochentag_geaendert = wochentag != letzter_wochentag;
    bool status_geaendert = strcmp(status, letzter_status) != 0;

    if (uhrzeit_geaendert || wochentag_geaendert || status_geaendert) {
        lvgl_port_lock(0);
        if (uhrzeit_geaendert)
            lv_label_set_text(s_uhr_label, uhrzeit);
        if (wochentag_geaendert)
            lv_label_set_text(s_wochentag_label, wochentag);
        if (status_geaendert)
            lv_label_set_text(s_status_label, status);
        lvgl_port_unlock();

        snprintf(letzte_uhrzeit, sizeof letzte_uhrzeit, "%s", uhrzeit);
        letzter_wochentag = wochentag;
        snprintf(letzter_status, sizeof letzter_status, "%s", status);
    }

    /* Farbschema/Sichtbarkeit nur bei tatsaechlichem Moduswechsel setzen -
     * betrifft den ganzen Bildschirm, ein Style-Update jede Sekunde
     * fuehrte sonst zu sichtbarem Flackern. Startwert bewusst kein
     * gueltiger anzeige_modus_t-Wert, damit der allererste Aufruf IMMER
     * als "Wechsel" zaehlt und modus_anwenden() garantiert einmal laeuft -
     * sonst bekaemen z. B. die Status-Symbole (die ihre Farbe nur dort
     * gesetzt bekommen, siehe status_icon_farbe_setzen) beim Start tags
     * ihre Farbe nie zugewiesen, da MODUS_TAG bereits der uebliche
     * Startzustand ist und sich der Modus dann nie "aendert". */
    static anzeige_modus_t letzter_modus = (anzeige_modus_t)-1;
    if (modus != letzter_modus) {
        modus_anwenden(modus);
        letzter_modus = modus;
    }

    /* Wochentag-Buttons der Tagesansicht - intern gegen unnoetige Updates
     * abgesichert (nur bei tatsaechlichem Tageswechsel). */
    tagesansicht_tag_aktualisieren();

    /* Status-Symbole rechts oben (WLAN/Zeit/Kalender) nur bei tatsaechlicher
     * Aenderung durchstreichen/wieder freigeben - kein Redraw jede Sekunde.
     * "einmalig" erzwingt beim allerersten Aufruf ein korrektes Anfangsbild,
     * auch falls der tatsaechliche Zustand zufaellig den Default trifft. */
    static bool einmalig = true;
    static bool letzter_wlan_ok = true;
    static bool letzter_zeit_ok = true;
    static bool letzter_kalender_ok = true;
    /* "ok" heisst hier bewusst mehr als "irgendein Wert vorhanden" - eine
     * manuell gesetzte Zeit bzw. rein aus dem Cache geparste Kalenderdaten
     * (Offline-Betrieb, siehe einrichtung.c) sollen als nicht bestaetigt
     * durchgestrichen bleiben, bis NTP bzw. ein echter Download gelingt. */
    bool wlan_ok = netz_ist_verbunden();
    bool zeit_ok = zeit_ist_synchron() && !zeit_ist_manuell_gesetzt();
    bool kalender_ok = kalender_anzeige_version() != 0 && kalender_anzeige_frisch();

    if (einmalig || wlan_ok != letzter_wlan_ok) {
        status_icon_ok_setzen(&s_status_wlan, wlan_ok);
        letzter_wlan_ok = wlan_ok;
    }
    if (einmalig || zeit_ok != letzter_zeit_ok) {
        status_icon_ok_setzen(&s_status_zeit, zeit_ok);
        letzter_zeit_ok = zeit_ok;
    }
    if (einmalig || kalender_ok != letzter_kalender_ok) {
        status_icon_ok_setzen(&s_status_kalender, kalender_ok);
        letzter_kalender_ok = kalender_ok;
    }
    einmalig = false;

    /* Termine/Tabletten: Text wird bei jedem Tick neu gebaut (billig, nur
     * ein kurzer Mutex + Stringformatierung), aber nur bei tatsaechlicher
     * Aenderung ans Label geschickt (wie bei uhrzeit/wochentag/status oben).
     * Ein reines Versionsgate (nur bei Kalender-Refresh) wuerde zwei Faelle
     * verpassen: (1) eine per Touch abgehakte Tablette aendert die Version
     * NICHT, (2) ein Termin "rutscht in die Vergangenheit", ohne dass sich
     * am Kalender selbst etwas aendert. */
    bool hat_daten = kalender_anzeige_version() != 0;
    static char tabletten_text[KALENDER_TEXT_MAX] = "...";
    static char termine_text[KALENDER_TEXT_MAX] = "...";
    char neuer_tabletten_text[KALENDER_TEXT_MAX];
    char neuer_termine_text[KALENDER_TEXT_MAX];

    kalender_tag_eintrag_t eintraege[KALENDER_EINTRAEGE_MAX];
    int anzahl = 0;
    bool zeit_bekannt = false;
    int jetzt_minuten = 0;

    if (hat_daten) {
        anzahl = kalender_anzeige_heutige_eintraege(eintraege, KALENDER_EINTRAEGE_MAX);

        zeit_bekannt = zeit_ist_synchron();
        if (zeit_bekannt) {
            time_t t = time(NULL);
            struct tm lokal_jetzt;
            localtime_r(&t, &lokal_jetzt);
            jetzt_minuten = lokal_jetzt.tm_hour * 60 + lokal_jetzt.tm_min;
        }

        liste_text_aufbauen(eintraege, anzahl, true, zeit_bekannt, jetzt_minuten,
                             neuer_tabletten_text, sizeof neuer_tabletten_text);
        liste_text_aufbauen(eintraege, anzahl, false, zeit_bekannt, jetzt_minuten,
                             neuer_termine_text, sizeof neuer_termine_text);
    } else {
        snprintf(neuer_tabletten_text, sizeof neuer_tabletten_text, "...");
        snprintf(neuer_termine_text, sizeof neuer_termine_text, "...");
    }

    bool tabletten_geaendert = strcmp(neuer_tabletten_text, tabletten_text) != 0;
    bool termine_geaendert = strcmp(neuer_termine_text, termine_text) != 0;
    if (!tabletten_geaendert && !termine_geaendert)
        return;

    lvgl_port_lock(0);
    if (tabletten_geaendert) {
        if (hat_daten)
            uebersicht_spalte_neu_aufbauen(&s_tabletten_spalte, UEBERSICHT_SPALTE_BREITE,
                                            eintraege, anzahl, true, zeit_bekannt, jetzt_minuten);
        else
            uebersicht_spalte_platzhalter_setzen(&s_tabletten_spalte, "...");
    }
    if (termine_geaendert) {
        if (hat_daten)
            uebersicht_spalte_neu_aufbauen(&s_termine_spalte, UEBERSICHT_SPALTE_BREITE,
                                            eintraege, anzahl, false, zeit_bekannt, jetzt_minuten);
        else
            uebersicht_spalte_platzhalter_setzen(&s_termine_spalte, "...");
    }
    lvgl_port_unlock();

    snprintf(tabletten_text, sizeof tabletten_text, "%s", neuer_tabletten_text);
    snprintf(termine_text, sizeof termine_text, "%s", neuer_termine_text);
}

/* Eine Boot-Phase hat ihren 60s-Countdown (Ring auf dem Startbildschirm)
 * aufgebraucht - Neustart, ein weiteres Warten bringt erfahrungsgemaess
 * nichts mehr (haengende Verbindung, DHCP-/DNS-Probleme, ...). */
static void phase_fehlgeschlagen_neustart(const char *phase)
{
    ESP_LOGE(TAG, "Start: Phase '%s' nicht in %ds abgeschlossen - Neustart",
             phase, STARTBILDSCHIRM_PHASE_TIMEOUT_S);
    vTaskDelay(pdMS_TO_TICKS(100)); /* Log-Ausgabe rausschreiben lassen */
    esp_restart();
}

static bool wlan_verbunden_pruefen(void) { return netz_ist_verbunden(); }
static bool zeit_synchron_pruefen(void) { return zeit_ist_synchron(); }
static bool kalender_bereit_pruefen(void) { return kalender_anzeige_version() != 0; }

typedef bool (*bedingung_fn)(void);

typedef enum {
    PHASE_FERTIG,
    PHASE_TIMEOUT,
    PHASE_WLAN_WECHSELN,
    PHASE_OFFLINE,
    PHASE_EINSTELLUNGEN,
} phase_ergebnis_t;

/* Wartet auf `bedingung`, bis entweder erfuellt, der 60s-Countdown
 * abgelaufen ist, oder der Benutzer einen der beiden Hilfe-Buttons auf dem
 * Startbildschirm antippt. */
static phase_ergebnis_t phase_abwarten(startbildschirm_schritt_t schritt, bedingung_fn bedingung)
{
    startbildschirm_schritt_start(schritt);
    for (int i = 0; i < STARTBILDSCHIRM_PHASE_TIMEOUT_S * 10; i++) {
        if (bedingung())
            return PHASE_FERTIG;
        switch (startbildschirm_aktion_abfragen()) {
        case STARTBILDSCHIRM_AKTION_WLAN_WECHSELN:
            return PHASE_WLAN_WECHSELN;
        case STARTBILDSCHIRM_AKTION_OFFLINE:
            return PHASE_OFFLINE;
        case STARTBILDSCHIRM_AKTION_EINSTELLUNGEN:
            return PHASE_EINSTELLUNGEN;
        default:
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return PHASE_TIMEOUT;
}

/* Verschachtelte Unternavigation innerhalb des Einstellungen-Menues: WLAN-
 * oder Datum-Bildschirm wird ueber dem Menue geoeffnet, nach dessen
 * Ende kehrt die Funktion (ueber eine neu aufgebaute Menue-Instanz) dorthin
 * zurueck - genau wie schon fuer "WLAN wechseln"/"Offline" auf dem
 * Startbildschirm selbst (siehe phase_verarbeiten), inklusive derselben
 * Reihenfolge "neuen Screen erst laden, dann alten erst loeschen". */
static void einstellungen_bildschirm_verarbeiten(void)
{
    einrichtung_einstellungen_zeigen();
    for (;;) {
        einrichtung_status_t status = einrichtung_einstellungen_status();
        if (status != EINRICHTUNG_OFFEN)
            break;

        switch (einrichtung_einstellungen_aktion_abfragen()) {
        case EINSTELLUNGEN_AKTION_WLAN: {
            einrichtung_wlan_zeigen();
            einrichtung_status_t wlan_status;
            while ((wlan_status = einrichtung_wlan_status()) == EINRICHTUNG_OFFEN)
                vTaskDelay(pdMS_TO_TICKS(100));
            (void)wlan_status; /* nur ABGEBROCHEN erreichbar - Speichern startet neu */
            einrichtung_einstellungen_zeigen();
            einrichtung_wlan_aufraeumen();
            break;
        }
        case EINSTELLUNGEN_AKTION_DATUM: {
            einrichtung_zeit_zeigen();
            einrichtung_status_t zeit_status;
            while ((zeit_status = einrichtung_zeit_status()) == EINRICHTUNG_OFFEN)
                vTaskDelay(pdMS_TO_TICKS(100));
            (void)zeit_status;
            einrichtung_einstellungen_zeigen();
            einrichtung_zeit_aufraeumen();
            break;
        }
        case EINSTELLUNGEN_AKTION_KALENDER_URL: {
            einrichtung_kalenderurl_zeigen();
            einrichtung_status_t url_status;
            while ((url_status = einrichtung_kalenderurl_status()) == EINRICHTUNG_OFFEN)
                vTaskDelay(pdMS_TO_TICKS(100));
            (void)url_status; /* Speichern und Abbrechen fuehren beide zurueck ins Menue */
            einrichtung_einstellungen_zeigen();
            einrichtung_kalenderurl_aufraeumen();
            break;
        }
        default:
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    /* Aufraeumen bewusst NICHT hier - der Aufrufer laedt zuerst den
     * Startbildschirm wieder als aktiven Screen, bevor dieser (dann
     * inaktive) Screen geloescht wird (siehe phase_verarbeiten). */
}

/* Wartet auf eine Boot-Phase und kuemmert sich um die Eingriffsmoeglich-
 * keiten: "WLAN wechseln" oeffnet die Zugangsdaten-Eingabe (bei "Speichern"
 * startet das Geraet neu, bei "Abbrechen" wird dieselbe Phase erneut
 * abgewartet); "Offline" oeffnet die Datum/Uhrzeit-Eingabe und markiert die
 * AKTUELL laufende Phase danach als erledigt - das gilt auch fuer die
 * WLAN-Phase selbst, denn ab dann laeuft das Geraet bewusst ohne Netz weiter
 * (Kalenderdaten kommen dann hoechstens aus dem Cache, siehe
 * kalender_anzeige.c); das Zahnrad-Symbol oeffnet das Einstellungen-Menue
 * (siehe einstellungen_bildschirm_verarbeiten), nach dessen Schliessen wird
 * die Phase ebenfalls erneut abgewartet (frischer Countdown). */
static void phase_verarbeiten(startbildschirm_schritt_t schritt, const char *name, bedingung_fn bedingung)
{
    for (;;) {
        phase_ergebnis_t ergebnis = phase_abwarten(schritt, bedingung);

        if (ergebnis == PHASE_FERTIG) {
            startbildschirm_schritt_fertig(schritt);
            return;
        }
        if (ergebnis == PHASE_TIMEOUT) {
            phase_fehlgeschlagen_neustart(name);
            return; /* unerreichbar */
        }

        if (ergebnis == PHASE_EINSTELLUNGEN) {
            netz_watchdog_pausieren(true);
            einstellungen_bildschirm_verarbeiten();
            startbildschirm_reaktivieren();
            einrichtung_einstellungen_aufraeumen();
            netz_watchdog_pausieren(false);
            continue; /* Phase erneut abwarten */
        }

        if (ergebnis == PHASE_WLAN_WECHSELN) {
            /* Waehrend der Eingabe darf ein WLAN-Abbruch im Hintergrund
             * keinen ueberraschenden Neustart ausloesen (der Watchdog
             * zaehlt unabhaengig vom angezeigten Bildschirm weiter). */
            netz_watchdog_pausieren(true);
            einrichtung_wlan_zeigen();
            einrichtung_status_t status;
            while ((status = einrichtung_wlan_status()) == EINRICHTUNG_OFFEN)
                vTaskDelay(pdMS_TO_TICKS(100));
            /* Erst den Startbildschirm wieder AKTIVIEREN, dann erst den
             * (jetzt inaktiven) Einrichtungsbildschirm loeschen - in der
             * umgekehrten Reihenfolge waere zwischen den beiden separaten
             * lvgl_port_lock()-Bloecken kurz kein aktiver Screen gesetzt,
             * waehrend der LVGL-Task parallel weiterlaeuft (haengt sich
             * dann teilweise auf, aehnlich dem Absturz aus Fallstricke #7). */
            startbildschirm_reaktivieren();
            einrichtung_wlan_aufraeumen();
            netz_watchdog_pausieren(false);
            (void)status; /* nur ABGEBROCHEN erreichbar - Speichern startet neu */
            continue;     /* Phase erneut abwarten */
        }

        /* PHASE_OFFLINE */
        netz_watchdog_pausieren(true);
        einrichtung_zeit_zeigen();
        einrichtung_status_t status;
        while ((status = einrichtung_zeit_status()) == EINRICHTUNG_OFFEN)
            vTaskDelay(pdMS_TO_TICKS(100));
        startbildschirm_reaktivieren();
        einrichtung_zeit_aufraeumen();
        netz_watchdog_pausieren(false);

        if (status == EINRICHTUNG_UEBERNOMMEN) {
            ESP_LOGI(TAG, "Start: Uhrzeit manuell gesetzt waehrend Phase '%s' - Phase als erledigt markiert", name);
            startbildschirm_schritt_fertig(schritt);
            return;
        }
        /* Abgebrochen -> Phase erneut abwarten */
    }
}

static void einblend_fertig_cb(lv_anim_t *a)
{
    (void)a;
    /* Die Abstandsbremse in einblend_anim_cb kann den letzten Zwischenschritt
     * uebersprungen haben - hier den echten Zielwert erzwingen, damit das
     * Overlay garantiert exakt auf der Zieldeckkraft landet. */
    lv_obj_set_style_bg_opa(s_dimm_overlay, overlay_ziel_fuer_modus(aktueller_modus()), 0);
    ESP_LOGI(TAG, "Start: Einblend-Animation fertig");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Start: Seniorenuhr startet (letzter Neustart-Grund: %s)",
             reset_grund_text(esp_reset_reason()));

    /* Ganz zuerst - initialisiert bei Bedarf selbst das NVS. */
    einstellungen_laden();

    zeit_zeitzone_setzen();

    ESP_ERROR_CHECK(anzeige_start());
    ESP_LOGI(TAG, "Start: Display bereit");
    ui_aufbauen();            /* baut auf einem eigenen, noch verborgenen Screen */
    ESP_LOGI(TAG, "Start: Hauptbildschirm aufgebaut (noch verborgen)");
    startbildschirm_erstellen(); /* zeigt sich auf dem aktuell aktiven Default-Screen */
    ESP_LOGI(TAG, "Start: Startbildschirm angezeigt");

    /* Jede Boot-Phase muss innerhalb des Ring-Countdowns gelingen - sonst
     * hilft nur ein Neustart (haengende Verbindungen, DHCP-Probleme, ...).
     * Der Ring auf dem Startbildschirm zeigt genau diese Restzeit an; nach
     * 30s bietet er zusaetzlich "WLAN wechseln" und "Offline" an. */
    netz_start();
    phase_verarbeiten(STARTBILDSCHIRM_WLAN, "WLAN", wlan_verbunden_pruefen);
    ESP_LOGI(TAG, "Start: Schritt WLAN fertig");

    zeit_sntp_starten();
    phase_verarbeiten(STARTBILDSCHIRM_UHR, "Uhrzeit (NTP)", zeit_synchron_pruefen);
    ESP_LOGI(TAG, "Start: Schritt Uhr fertig");

    kalender_task_starten();
    phase_verarbeiten(STARTBILDSCHIRM_KALENDER, "Kalender", kalender_bereit_pruefen);
    ESP_LOGI(TAG, "Start: Schritt Kalender fertig (version=%lu)",
             (unsigned long)kalender_anzeige_version());

    vTaskDelay(pdMS_TO_TICKS(2000)); /* alle drei Symbole kurz weiss stehen lassen */
    ESP_LOGI(TAG, "Start: Wechsle zur Hauptanzeige");

    /* Overlay VOR dem Bildschirmwechsel auf voll schwarz setzen - sonst
     * waere die Hauptanzeige fuer mindestens einen Frame mit den (noch
     * unkorrigierten) Default-Farben aus ui_aufbauen() sichtbar, bevor
     * ueberhaupt etwas abgedunkelt wird. */
    lvgl_port_lock(0);
    lv_obj_set_style_bg_opa(s_dimm_overlay, LV_OPA_COVER, 0);
    lv_screen_load(s_bildschirm);
    lvgl_port_unlock();
    ESP_LOGI(TAG, "Start: Bildschirm gewechselt (schwarz ueberdeckt)");
    startbildschirm_aufraeumen();

    /* Jetzt die echten Farben/Texte setzen - unsichtbar, da das Overlay
     * noch komplett deckt. */
    uhr_tick(NULL);
    ESP_LOGI(TAG, "Start: Werte gesetzt, starte Einblend-Animation");

    lv_opa_t einblend_ziel = overlay_ziel_fuer_modus(aktueller_modus());

    /* lv_anim- und lv_timer_create-Aufrufe sind LVGL-Kernfunktionen -
     * ohne Lock hier lief "main" mit dem LVGL-Task in einen Datenwettlauf
     * auf dessen interne Listen, der main() in eine Endlosschleife trieb
     * (Task-Watchdog-Absturz, sichtbar als haengenbleibender schwarzer
     * Bildschirm nach einem Kaltstart). */
    lvgl_port_lock(0);
    static lv_anim_t einblend_anim;
    lv_anim_init(&einblend_anim);
    lv_anim_set_var(&einblend_anim, s_dimm_overlay);
    lv_anim_set_values(&einblend_anim, LV_OPA_COVER, einblend_ziel);
    lv_anim_set_time(&einblend_anim, 2000);
    lv_anim_set_path_cb(&einblend_anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&einblend_anim, einblend_anim_cb);
    lv_anim_set_completed_cb(&einblend_anim, einblend_fertig_cb);
    lv_anim_start(&einblend_anim);

    lv_timer_create(uhr_tick, 1000, NULL);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "Start: Uhr laeuft");
}
