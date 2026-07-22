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
#include <math.h>
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
#include "screenshot_debug.h"
#include "startbildschirm.h"
#include "tagesansicht.h"
#include "zeit.h"

/* Entwicklungswerkzeuge, die nur auf dem Entwicklungsboard laufen sollen,
 * nicht auf dem Geraet bei den Eltern. Fuer einen Produktions-Build (das
 * Geraet, das rausgeht) auf 0 setzen und neu bauen - dann faellt der
 * Screenshot-Button weg und der ungenutzte screenshot_debug-Code wird vom
 * Linker (gc-sections) ganz aus dem Binary geworfen. Aktuell steuert der
 * Schalter nur den Screenshot-Button. NICHT betroffen: der Demo-Modus im
 * Einstellungsmenue - der bleibt bewusst auch auf dem Eltern-Geraet
 * verfuegbar (Peters Wunsch). */
#define ENTWICKLUNGSWERKZEUGE 1

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
#define FARBE_ZEIT_UNBESTAETIGT 0xcc7a1a /* dunkles Orange: grosse Uhrzeit blinkt damit, solange die Zeit nicht per NTP bestaetigt ist */
#define FARBE_TABLETTE_FAELLIG      FARBE_AKZENT /* faellig, noch unbestaetigt: dasselbe Gold wie Wochentag/Ueberschriften */
#define FARBE_TABLETTE_UEBERFAELLIG FARBE_WARNUNG /* seit KALENDER_TABLETTE_UEBERFAELLIG_MIN unbestaetigt: dasselbe Rot wie bei den Status-Symbolen */

/* Kantenlaenge der kleinen Status-Symbole rechts oben (WLAN/Zeit/Kalender). */
#define STATUS_ICON_GROESSE 34
#define STATUS_ICON_MAX_TEILE 4

/* Analoge Zusatzuhr (Peters Idee): per Tipp lassen sich Digital- und
 * Analoganzeige tauschen - eine ist immer "gross" (Bildschirmmitte, wie
 * bisher die Digitaluhr), die andere "klein" (rechter Bereich, zwischen den
 * Status-Symbolen oben und der Tabletten/Termine-Uebersicht unten). Beide
 * Darstellungen bleiben dabei immer sichtbar, nur Groesse/Position tauschen -
 * das haelt den "--:--"-Hinweis bei unbekannter Zeit immer sichtbar, egal
 * welche Ansicht gerade klein ist. Start (nach jedem Neustart): Digital
 * gross, wie bisher - Peters Wunsch, kein persistenter Zustand noetig.
 *
 * Beide Anzeigen bleiben direkte Kinder von s_bildschirm (kein eigener
 * Slot-Container!) - ein erster Versuch mit einem 170px breiten Container
 * fuer die Digitaluhr schnitt deren Text sichtbar ab: bei schrift_uhr_128
 * braucht schon "00:00" weit mehr als 170px Breite, waehrend ein Container
 * Kinder standardmaessig an seiner eigenen Kante abschneidet. Positions-
 * berechnung erfolgt deshalb direkt in Bildschirmkoordinaten (Centerpunkt),
 * nie ueber die Breite/Groesse eines begrenzenden Elternobjekts. */
#define ANALOG_UHR_TICKS 12
/* 160 statt urspruenglich 200: der Platz zwischen Wochentag-Schriftzug oben
 * und Tageszeit/Datum-Zeile unten betraegt nur ~190px - mit 200px klebte
 * der Kreis an beiden Schriftzuegen ("wirkt sehr gedrungen", Peter). */
#define UHR_ANALOG_DURCHMESSER_GROSS 160
#define UHR_ANALOG_DURCHMESSER_KLEIN 90
#define UHR_SLOT_GROSS_CX 400
#define UHR_SLOT_GROSS_CY 180
#define UHR_SLOT_KLEIN_CX 710
#define UHR_SLOT_KLEIN_CY 175
#define UHR_PI 3.14159265358979323846f

typedef struct {
    lv_obj_t *container;
    lv_obj_t *ring;
    lv_obj_t *ticks[ANALOG_UHR_TICKS];
    lv_point_precise_t tick_punkte[ANALOG_UHR_TICKS][2];
    lv_obj_t *stundenzeiger;
    lv_obj_t *minutenzeiger;
    lv_point_precise_t stunden_punkte[2];
    lv_point_precise_t minuten_punkte[2];
    int32_t durchmesser;
} analog_uhr_t;

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
static analog_uhr_t s_analog_uhr;
static bool s_uhr_analog_ist_gross = false; /* Start: Digital gross (Peters Wunsch) */
/* Letzte per NTP bekannte Uhrzeit - fuer analog_uhr_zeiger_aktualisieren()
 * unmittelbar nach einem Tausch (siehe uhr_tausch_cb), auch wenn sich die
 * Minute seit dem letzten Tick nicht geaendert hat. -1 = noch unbekannt. */
static int s_zeit_stunde = -1;
static int s_zeit_minute = -1;
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

/* Normale (nicht-blinkende) Textfarbe der grossen Uhrzeit im jeweiligen
 * Modus - ausgelagert, damit uhr_tick() sie auch fuer das Zurueckschalten
 * nach dem Unbestaetigt-Blinken kennt (siehe FARBE_ZEIT_UNBESTAETIGT). */
static uint32_t uhrzeit_farbe_fuer_modus(anzeige_modus_t modus)
{
    return (modus == MODUS_NACHT) ? FARBE_NACHT_TEXT : FARBE_TEXT_HELL;
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

        bool hat_farbe;
        uint32_t farbe = FARBE_VERGANGEN;
        if (nur_tabletten) {
            switch (kalender_tablette_status(&eintraege[i], zeit_bekannt, jetzt_minuten)) {
            case KALENDER_TABLETTE_ABGEHAKT:     farbe = FARBE_VERGANGEN; hat_farbe = true; break;
            case KALENDER_TABLETTE_FAELLIG:      farbe = FARBE_TABLETTE_FAELLIG; hat_farbe = true; break;
            case KALENDER_TABLETTE_UEBERFAELLIG: farbe = FARBE_TABLETTE_UEBERFAELLIG; hat_farbe = true; break;
            default:                             hat_farbe = false; break;
            }
        } else {
            hat_farbe = zeit_bekannt && !eintraege[i].ganztags &&
                        (eintraege[i].stunde * 60 + eintraege[i].minute) < jetzt_minuten;
        }

        char zeile[104];
        if (hat_farbe)
            snprintf(zeile, sizeof zeile, "#%06lx %s#\n", (unsigned long)farbe, inhalt);
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

        uint32_t farbe = FARBE_TEXT_HELL;
        if (nur_tabletten) {
            switch (kalender_tablette_status(&eintraege[i], zeit_bekannt, jetzt_minuten)) {
            case KALENDER_TABLETTE_ABGEHAKT:     farbe = FARBE_VERGANGEN; break;
            case KALENDER_TABLETTE_FAELLIG:      farbe = FARBE_TABLETTE_FAELLIG; break;
            case KALENDER_TABLETTE_UEBERFAELLIG: farbe = FARBE_TABLETTE_UEBERFAELLIG; break;
            case KALENDER_TABLETTE_ZUKUNFT:      farbe = FARBE_TEXT_HELL; break;
            }
        } else if (vergangen) {
            farbe = FARBE_VERGANGEN;
        }

        char inhalt[88];
        const char *praefix = abgehakt ? UEBERSICHT_HAKEN_PRAEFIX : "";
        if (eintraege[i].ganztags)
            snprintf(inhalt, sizeof inhalt, "%s%s", praefix, eintraege[i].titel);
        else
            snprintf(inhalt, sizeof inhalt, "%s%02d:%02d  %s", praefix,
                     eintraege[i].stunde, eintraege[i].minute, eintraege[i].titel);

        lv_obj_t *label = lv_label_create(spalte->container);
        lv_obj_set_style_text_font(label, &schrift_klein_28, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(farbe), 0);
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
static lv_obj_t *s_status_fenster;
static lv_timer_t *s_status_fenster_timer;

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

/* Baut Ring, 12 Stundenstriche und beide Zeiger als Kinder von `parent`
 * (einem der beiden Uhr-Slots) auf. Positionen/Groessen kommen erst danach
 * ueber analog_uhr_layout_anwenden() - hier nur die Objekte selbst. */
static void analog_uhr_erzeugen(analog_uhr_t *uhr, lv_obj_t *parent)
{
    uhr->container = lv_obj_create(parent);
    lv_obj_remove_style_all(uhr->container);
    lv_obj_remove_flag(uhr->container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(uhr->container, LV_OBJ_FLAG_CLICKABLE);

    uhr->ring = lv_obj_create(uhr->container);
    lv_obj_remove_style_all(uhr->ring);
    lv_obj_set_style_radius(uhr->ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(uhr->ring, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(uhr->ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(uhr->ring, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < ANALOG_UHR_TICKS; i++) {
        lv_obj_t *strich = lv_line_create(uhr->container);
        lv_obj_set_style_line_rounded(strich, true, 0);
        lv_obj_remove_flag(strich, LV_OBJ_FLAG_CLICKABLE);
        uhr->ticks[i] = strich;
    }

    uhr->stundenzeiger = lv_line_create(uhr->container);
    lv_obj_set_style_line_rounded(uhr->stundenzeiger, true, 0);
    lv_obj_remove_flag(uhr->stundenzeiger, LV_OBJ_FLAG_CLICKABLE);

    uhr->minutenzeiger = lv_line_create(uhr->container);
    lv_obj_set_style_line_rounded(uhr->minutenzeiger, true, 0);
    lv_obj_remove_flag(uhr->minutenzeiger, LV_OBJ_FLAG_CLICKABLE);
}

/* Setzt Farbe von Ring/Strichen/Zeigern - dieselbe Farbe wie die Digitaluhr
 * im jeweiligen Tag-/Abend-/Nacht-Modus (siehe modus_anwenden). */
static void analog_uhr_farbe_setzen(analog_uhr_t *uhr, lv_color_t farbe)
{
    lv_obj_set_style_border_color(uhr->ring, farbe, 0);
    for (int i = 0; i < ANALOG_UHR_TICKS; i++)
        lv_obj_set_style_line_color(uhr->ticks[i], farbe, 0);
    lv_obj_set_style_line_color(uhr->stundenzeiger, farbe, 0);
    lv_obj_set_style_line_color(uhr->minutenzeiger, farbe, 0);
}

/* Passt Groesse/Position/Strichstaerken an einen der beiden Uhr-Plaetze
 * (gross/klein, per Mittelpunkt cx/cy in Bildschirmkoordinaten) an - wird
 * bei jedem Tausch aufgerufen. Positioniert die 12 Stundenstriche neu (ihre
 * Position haengt nur vom Durchmesser ab, nicht von der Uhrzeit - die
 * Zeiger selbst aktualisiert separat analog_uhr_zeiger_aktualisieren()). */
static void analog_uhr_layout_anwenden(analog_uhr_t *uhr, int32_t durchmesser, int32_t cx, int32_t cy)
{
    uhr->durchmesser = durchmesser;
    bool gross = durchmesser >= UHR_ANALOG_DURCHMESSER_GROSS;

    lv_obj_set_size(uhr->container, durchmesser, durchmesser);
    lv_obj_set_pos(uhr->container, cx - durchmesser / 2, cy - durchmesser / 2);

    lv_obj_set_size(uhr->ring, durchmesser, durchmesser);
    lv_obj_set_pos(uhr->ring, 0, 0);
    lv_obj_set_style_border_width(uhr->ring, gross ? 4 : 2, 0);

    lv_obj_set_style_line_width(uhr->stundenzeiger, gross ? 7 : 4, 0);
    lv_obj_set_style_line_width(uhr->minutenzeiger, gross ? 5 : 3, 0);
    for (int i = 0; i < ANALOG_UHR_TICKS; i++)
        lv_obj_set_style_line_width(uhr->ticks[i], gross ? 3 : 2, 0);

    int32_t mitte = durchmesser / 2;
    int32_t aussen = mitte - (gross ? 6 : 4);
    int32_t innen = aussen - (gross ? 16 : 9);
    for (int i = 0; i < ANALOG_UHR_TICKS; i++) {
        float winkel = (float)i * (2.0f * UHR_PI / ANALOG_UHR_TICKS);
        float s = sinf(winkel), c = cosf(winkel);
        uhr->tick_punkte[i][0].x = mitte + (int32_t)(s * innen);
        uhr->tick_punkte[i][0].y = mitte - (int32_t)(c * innen);
        uhr->tick_punkte[i][1].x = mitte + (int32_t)(s * aussen);
        uhr->tick_punkte[i][1].y = mitte - (int32_t)(c * aussen);
        lv_line_set_points(uhr->ticks[i], uhr->tick_punkte[i], 2);
    }
}

/* Richtet Stunden-/Minutenzeiger auf die uebergebene Uhrzeit aus - fuer den
 * aktuell gueltigen Durchmesser (siehe analog_uhr_layout_anwenden). Wird
 * einmal pro Minute (uhr_tick) sowie sofort nach jedem Tausch aufgerufen,
 * NIE jede Sekunde (Peters Wunsch: kein Sekundenzeiger, ruhiges Bild). */
static void analog_uhr_zeiger_aktualisieren(analog_uhr_t *uhr, int stunde, int minute)
{
    if (stunde < 0)
        return; /* Uhrzeit noch nicht bekannt - Zeiger unveraendert lassen */

    int32_t mitte = uhr->durchmesser / 2;
    float winkel_minute = (float)minute * (2.0f * UHR_PI / 60.0f);
    float winkel_stunde = ((float)(stunde % 12) + (float)minute / 60.0f) * (2.0f * UHR_PI / 12.0f);

    int32_t laenge_minute = mitte - mitte / 6;
    int32_t laenge_stunde = mitte - mitte / 2;

    uhr->minuten_punkte[0].x = mitte;
    uhr->minuten_punkte[0].y = mitte;
    uhr->minuten_punkte[1].x = mitte + (int32_t)(sinf(winkel_minute) * (float)laenge_minute);
    uhr->minuten_punkte[1].y = mitte - (int32_t)(cosf(winkel_minute) * (float)laenge_minute);
    lv_line_set_points(uhr->minutenzeiger, uhr->minuten_punkte, 2);

    uhr->stunden_punkte[0].x = mitte;
    uhr->stunden_punkte[0].y = mitte;
    uhr->stunden_punkte[1].x = mitte + (int32_t)(sinf(winkel_stunde) * (float)laenge_stunde);
    uhr->stunden_punkte[1].y = mitte - (int32_t)(cosf(winkel_stunde) * (float)laenge_stunde);
    lv_line_set_points(uhr->stundenzeiger, uhr->stunden_punkte, 2);
}

/* Zentriert ein (bereits mit Text/Font versehenes) Label um den Punkt
 * cx/cy - fuer die Digitaluhr im "klein"-Platz, wo (anders als beim
 * schrift-breiten "gross"-Platz per LV_ALIGN_TOP_MID) die tatsaechliche,
 * variable Textbreite beruecksichtigt werden muss. Klemmt das Ergebnis
 * zusaetzlich auf den sichtbaren Bereich (0..800): "00:38" bei
 * schrift_mittel_40 ist breiter als zunaechst angenommen und ragte beim
 * naeher an den Rand geschobenen Slot ueber die 800px hinaus - dort
 * einfach unsichtbar, da der Bildschirm keine weiteren Pixel hat. Damit
 * muss die genaue Glyphenbreite nicht mehr vorab geschaetzt werden. */
static void label_an_punkt_zentrieren(lv_obj_t *label, int32_t cx, int32_t cy)
{
    /* WICHTIG: das Label traegt vom "gross"-Platz noch LV_ALIGN_TOP_MID als
     * Style-Align - lv_obj_set_pos() aendert in LVGL 9 nur die Offsets,
     * NICHT das Align. Ohne Ruecksetzen wuerden die hier berechneten
     * "absoluten" Koordinaten als Versatz von der oberen BildschirmMITTE
     * interpretiert und schoben das Label komplett aus dem sichtbaren
     * Bereich (live so passiert: nach dem Tausch war die kleine
     * Digitalanzeige schlicht unsichtbar). */
    lv_obj_set_align(label, LV_ALIGN_TOP_LEFT);
    lv_obj_update_layout(label);
    int32_t breite = lv_obj_get_width(label);
    int32_t x = cx - breite / 2;
    if (x < 4)
        x = 4;
    if (x + breite > 796)
        x = 796 - breite;
    lv_obj_set_pos(label, x, cy - lv_obj_get_height(label) / 2);
}

/* Bringt Digitaluhr-Label und Analoguhr an die jeweils richtige Position
 * (gross/klein) - Gegenstueck ist immer am jeweils anderen Platz. Die
 * Digitaluhr behaelt im "gross"-Fall bewusst ihre urspruengliche, auf die
 * volle Bildschirmbreite bezogene Zentrierung (LV_ALIGN_TOP_MID) statt
 * eines festen Punktes - das war schon immer robust gegenueber wechselnder
 * Textbreite (z. B. schmalere "1" vs. breitere "0"). */
static void uhr_tausch_anwenden(void)
{
    if (s_uhr_analog_ist_gross) {
        analog_uhr_layout_anwenden(&s_analog_uhr, UHR_ANALOG_DURCHMESSER_GROSS,
                                    UHR_SLOT_GROSS_CX, UHR_SLOT_GROSS_CY);
        lv_obj_set_style_text_font(s_uhr_label, &schrift_mittel_40, 0);
        label_an_punkt_zentrieren(s_uhr_label, UHR_SLOT_KLEIN_CX, UHR_SLOT_KLEIN_CY);
    } else {
        analog_uhr_layout_anwenden(&s_analog_uhr, UHR_ANALOG_DURCHMESSER_KLEIN,
                                    UHR_SLOT_KLEIN_CX, UHR_SLOT_KLEIN_CY);
        lv_obj_set_style_text_font(s_uhr_label, &schrift_uhr_128, 0);
        lv_obj_align(s_uhr_label, LV_ALIGN_TOP_MID, 0, 95);
    }
    analog_uhr_zeiger_aktualisieren(&s_analog_uhr, s_zeit_stunde, s_zeit_minute);
}

/* Tipp auf die Digitaluhr ODER die Analoguhr tauscht, welche der beiden
 * gerade gross (Bildschirmmitte) bzw. klein (rechter Bereich) dargestellt
 * wird - Peters Idee, je nach Vorliebe umschaltbar. Kein Persistieren in den
 * Einstellungen: nach jedem Neustart startet wieder Digital gross. */
static void uhr_tausch_cb(lv_event_t *e)
{
    (void)e;
    lvgl_port_lock(0);
    s_uhr_analog_ist_gross = !s_uhr_analog_ist_gross;
    uhr_tausch_anwenden();
    lvgl_port_unlock();
}

#define STATUS_FENSTER_ANZEIGEDAUER_MS 8000

/* Schreibt "vor Xh Ym"/"vor Ymin"/"vor Zs" bzw. "noch nie" nach ziel -
 * fuers Status-Detail-Fenster (Peters Idee: Tipp auf die Status-Symbole
 * zeigt Klartext-Details zu WLAN/Zeit/Kalender). */
static void seit_text_formatieren(time_t zeitpunkt, char *ziel, size_t ziel_groesse)
{
    if (zeitpunkt == 0) {
        snprintf(ziel, ziel_groesse, "noch nie");
        return;
    }
    long sekunden = (long)difftime(time(NULL), zeitpunkt);
    if (sekunden < 0)
        sekunden = 0;
    if (sekunden < 60)
        snprintf(ziel, ziel_groesse, "vor %lds", sekunden);
    else if (sekunden < 3600)
        snprintf(ziel, ziel_groesse, "vor %ldmin", sekunden / 60);
    else
        snprintf(ziel, ziel_groesse, "vor %ldh %ldmin", sekunden / 3600, (sekunden / 60) % 60);
}

static void status_fenster_intern_schliessen(void)
{
    if (s_status_fenster_timer) {
        lv_timer_delete(s_status_fenster_timer);
        s_status_fenster_timer = NULL;
    }
    if (s_status_fenster) {
        lv_obj_delete(s_status_fenster);
        s_status_fenster = NULL;
    }
}

static void status_fenster_timer_cb(lv_timer_t *t)
{
    (void)t;
    lvgl_port_lock(0);
    status_fenster_intern_schliessen();
    lvgl_port_unlock();
}

/* Erzeugt eine (moeglicherweise mehrzeilige) Status-Zeile bei y und liefert
 * die naechste freie Y-Position dahinter zurueck. Bei diesem grossen Font
 * (schrift_klein_28) bricht schon ein mittellanger Satz auf 2-3 Zeilen um -
 * die tatsaechliche Hoehe erst NACH lv_obj_update_layout() abzufragen (statt
 * eine feste Zeilenzahl zu raten) verhindert zuverlaessig ein Ueberlappen
 * mit der naechsten Zeile, unabhaengig vom genauen Umbruchpunkt. */
static int32_t status_zeile_erzeugen(lv_obj_t *parent, int32_t y, const char *text, bool ok)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, 400);
    lv_obj_set_style_text_font(label, &schrift_klein_28, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(ok ? FARBE_TEXT_HELL : FARBE_WARNUNG), 0);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, 24, y);
    lv_obj_update_layout(label);
    return y + lv_obj_get_height(label) + 16;
}

/* Tipp auf die Status-Symbole (WLAN/Zeit/Kalender rechts oben) oeffnet fuer
 * ein paar Sekunden ein Klartext-Fenster mit denselben "ok"-Kriterien wie
 * die Symbole selbst (siehe wlan_ok/zeit_ok/kalender_ok in uhr_tick). Stimmt
 * beim Kalender etwas nicht, wird gleich ein sofortiger Resync-Versuch
 * angestossen - fuer WLAN/NTP ist das unnoetig, die versuchen im
 * Hintergrund ohnehin schon fortlaufend automatisch die Wiederverbindung. */
static void status_detail_oeffnen_cb(lv_event_t *e)
{
    (void)e;

    bool wlan_ok = netz_ist_verbunden();
    bool zeit_ok = zeit_ist_synchron() && !zeit_ist_manuell_gesetzt();
    bool kalender_ok = kalender_anzeige_version() != 0 && kalender_anzeige_frisch();

    char ssid[33], ip[16];
    netz_ssid_text(ssid, sizeof ssid);
    netz_ip_text(ip, sizeof ip);

    char seit_zeit[32], seit_kalender[32];
    seit_text_formatieren(einstellungen_letzte_sync(), seit_zeit, sizeof seit_zeit);
    seit_text_formatieren(einstellungen_letzter_kalender_sync(), seit_kalender, sizeof seit_kalender);

    char zeile_wlan[96], zeile_zeit[96], zeile_kalender[96];
    if (wlan_ok)
        snprintf(zeile_wlan, sizeof zeile_wlan, "WLAN: %s (%d dBm)\nIP %s", ssid, netz_rssi_dbm(), ip);
    else
        snprintf(zeile_wlan, sizeof zeile_wlan, "WLAN: nicht verbunden");

    if (zeit_ok)
        snprintf(zeile_zeit, sizeof zeile_zeit, "Uhrzeit: synchronisiert (%s)", seit_zeit);
    else
        snprintf(zeile_zeit, sizeof zeile_zeit, "Uhrzeit: nicht bestaetigt\n(letzter Sync %s)", seit_zeit);

    if (kalender_ok)
        snprintf(zeile_kalender, sizeof zeile_kalender, "Kalender: aktuell (%s)", seit_kalender);
    else
        snprintf(zeile_kalender, sizeof zeile_kalender, "Kalender: veraltet\n(letzter Sync %s)", seit_kalender);

    if (!kalender_ok)
        kalender_anzeige_jetzt_pruefen();

    lvgl_port_lock(0);
    status_fenster_intern_schliessen();

    s_status_fenster = lv_obj_create(s_bildschirm);
    lv_obj_set_style_bg_color(s_status_fenster, lv_color_hex(0x0d1f3d), 0);
    lv_obj_set_style_bg_opa(s_status_fenster, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_status_fenster, lv_color_hex(FARBE_AKZENT), 0);
    lv_obj_set_style_border_width(s_status_fenster, 2, 0);
    lv_obj_set_style_radius(s_status_fenster, 12, 0);
    lv_obj_remove_flag(s_status_fenster, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_status_fenster, 448, 400);
    lv_obj_align(s_status_fenster, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *titel = lv_label_create(s_status_fenster);
    lv_label_set_text(titel, "STATUS");
    lv_obj_set_style_text_font(titel, &schrift_mittel_40, 0);
    lv_obj_set_style_text_color(titel, lv_color_hex(FARBE_AKZENT), 0);
    lv_obj_align(titel, LV_ALIGN_TOP_MID, 0, 15);

    int32_t y = 85;
    y = status_zeile_erzeugen(s_status_fenster, y, zeile_wlan, wlan_ok);
    y = status_zeile_erzeugen(s_status_fenster, y, zeile_zeit, zeit_ok);
    status_zeile_erzeugen(s_status_fenster, y, zeile_kalender, kalender_ok);

    s_status_fenster_timer = lv_timer_create(status_fenster_timer_cb, STATUS_FENSTER_ANZEIGEDAUER_MS, NULL);
    lv_timer_set_repeat_count(s_status_fenster_timer, 1);
    lvgl_port_unlock();
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
    lv_obj_add_flag(s_uhr_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_uhr_label, uhr_tausch_cb, LV_EVENT_CLICKED, NULL);

    /* Analoge Zusatzuhr (Peters Idee), startet klein rechts - direktes Kind
     * von s_bildschirm, siehe Erklaerung bei ANALOG_UHR_TICKS oben. */
    analog_uhr_erzeugen(&s_analog_uhr, s_bildschirm);
    analog_uhr_layout_anwenden(&s_analog_uhr, UHR_ANALOG_DURCHMESSER_KLEIN, UHR_SLOT_KLEIN_CX, UHR_SLOT_KLEIN_CY);
    lv_obj_add_event_cb(s_analog_uhr.container, uhr_tausch_cb, LV_EVENT_CLICKED, NULL);

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

    /* Unsichtbare, grosszuegige Tippflaeche ueber allen drei Status-
     * Symbolen (Peters Idee) - oeffnet das Status-Detail-Fenster. Bewusst
     * deutlich groesser als die 34px-Symbole selbst (seniorengerechtes,
     * leicht zu treffendes Ziel), reicht bis zur rechten/oberen Bildschirmkante,
     * wo sonst nichts anderes liegt. */
    lv_obj_t *status_tippflaeche = lv_obj_create(s_bildschirm);
    lv_obj_remove_style_all(status_tippflaeche);
    lv_obj_set_pos(status_tippflaeche, 620, 0);
    lv_obj_set_size(status_tippflaeche, 800 - 620, 64);
    lv_obj_add_flag(status_tippflaeche, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(status_tippflaeche, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(status_tippflaeche, status_detail_oeffnen_cb, LV_EVENT_CLICKED, NULL);

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
    analog_uhr_farbe_setzen(&s_analog_uhr, lv_color_hex(textfarbe_hell));
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

/* Einmal true (Demo-Modus im Einstellungen-Menue gewaehlt), ueberspringen
 * alle noch offenen Boot-Phasen ihren Countdown sofort - die Uhrzeit steht
 * dann schon auf dem festen Demo-Zeitstempel, Kalenderdaten kommen
 * hoechstens aus dem Cache (siehe einstellungen_bildschirm_verarbeiten). */
static bool s_demo_modus = false;

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
        /* Die fest verdrahtete Demo-Zeit nicht als "zuletzt angezeigt"
         * persistieren - sonst wuerde der Boot-Fallback (siehe
         * phase_timeout_automatisch_fortsetzen) nach einer Vorfuehrung
         * beim naechsten WLAN-Ausfall mit dem Fantasie-Datum starten.
         * Sobald NTP die Zeit doch noch bestaetigt (zeit_ist_manuell_gesetzt
         * wird dann false), ist die Sperre hinfaellig. */
        if (!s_demo_modus || !zeit_ist_manuell_gesetzt())
            einstellungen_letzte_anzeige_setzen(jetzt);

        snprintf(uhrzeit, sizeof uhrzeit, "%02d:%02d", lokal.tm_hour, lokal.tm_min);
        s_zeit_stunde = lokal.tm_hour;
        s_zeit_minute = lokal.tm_min;
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
        if (uhrzeit_geaendert) {
            lv_label_set_text(s_uhr_label, uhrzeit);
            /* Nur einmal pro Minute (Peters Wunsch: kein Sekundenzeiger) -
             * uhrzeit aendert sich als "HH:MM"-Text ohnehin nur dann. */
            analog_uhr_zeiger_aktualisieren(&s_analog_uhr, s_zeit_stunde, s_zeit_minute);
        }
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

    /* Grosse Uhrzeit blinkt dunkelorange, solange die Zeit nicht per NTP
     * bestaetigt ist (manuell eingegeben ODER automatisch vom letzten
     * bekannten Stand uebernommen, siehe zeit_uebernehmen) - deutlich
     * auffaelliger als nur das kleine durchgestrichene Status-Symbol.
     * Peters ausdruecklicher Wunsch: man soll auf den ersten Blick sehen,
     * dass die angezeigte Uhrzeit (noch) nicht bestaetigt ist. */
    bool zeit_unbestaetigt = zeit_ist_synchron() && zeit_ist_manuell_gesetzt();
    static bool zeit_blink_an = false;
    static bool zeit_war_unbestaetigt = false;
    if (zeit_unbestaetigt) {
        zeit_blink_an = !zeit_blink_an;
        lvgl_port_lock(0);
        lv_obj_set_style_text_color(s_uhr_label,
            lv_color_hex(zeit_blink_an ? FARBE_ZEIT_UNBESTAETIGT : uhrzeit_farbe_fuer_modus(modus)), 0);
        lvgl_port_unlock();
        zeit_war_unbestaetigt = true;
    } else if (zeit_war_unbestaetigt) {
        lvgl_port_lock(0);
        lv_obj_set_style_text_color(s_uhr_label, lv_color_hex(uhrzeit_farbe_fuer_modus(modus)), 0);
        lvgl_port_unlock();
        zeit_war_unbestaetigt = false;
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
 * aufgebraucht. Fruehere Fassung: Neustart. Problem dabei: ohne
 * batteriegepufferte RTC verliert das Geraet bei JEDEM Neustart die
 * Uhrzeit komplett, und die Software kann einen echten Stromausfall/eine
 * Spannungsschwankung nicht von einer (noch) fehlenden WLAN-Verbindung
 * unterscheiden. Bleibt WLAN laenger weg (schwaches Signal, Router-
 * Ausfall, ...), wuerde ein Neustart-Reflex in eine nie endende
 * Boot-Schleife fuehren, waehrend der die Anzeige DAUERHAFT schwarz
 * bliebe und faellige Tabletten/Termine gar nicht erst gezeigt wuerden -
 * der Super-GAU, den Peter nach dem WLAN-Watchdog-Vorfall ausdruecklich
 * vermeiden wollte (siehe FALLSTRICKE #14). Deshalb macht das Geraet
 * stattdessen automatisch weiter - wie beim manuellen "Offline"-Button,
 * nur ohne dass jemand danebenstehen muss: die Uhrzeit wird (falls noch
 * nicht bekannt) auf den zuletzt angezeigten Stand gesetzt (siehe
 * zeit_uebernehmen/einstellungen_letzte_anzeige) und als unbestaetigt
 * markiert (durchgestrichenes Status-Symbol, blinkende Uhrzeit - siehe
 * uhr_tick). Eine zeitweise falsche Uhrzeit ist das eindeutig kleinere
 * Uebel gegenueber einer dauerhaft schwarzen Anzeige.
 *
 * Der zuletzt angezeigte Stand liegt zu diesem Zeitpunkt schon eine Weile
 * zurueck (Boot bis hierher + ggf. bereits verstrichene Timeouts fruehe-
 * rer Phasen). esp_timer_get_time() laeuft seit dem Einschalten ununter-
 * brochen mit, unabhaengig davon, wie viele Phasen bereits in einen
 * Timeout gelaufen sind - die seither vergangene Zeit wird daher
 * addiert, damit die uebernommene Uhrzeit so nah wie moeglich an der
 * Wahrheit liegt (statt um die gesamte Boot-/Wartezeit nachzugehen). */
static void phase_timeout_automatisch_fortsetzen(startbildschirm_schritt_t schritt, const char *phase)
{
    ESP_LOGW(TAG, "Start: Phase '%s' nicht in %ds abgeschlossen - mache automatisch weiter "
                  "(letzter bekannter Zeitstand, Anzeige hat Prioritaet)",
             phase, STARTBILDSCHIRM_PHASE_TIMEOUT_S);
    if (!zeit_ist_synchron()) {
        time_t letzte_anzeige = einstellungen_letzte_anzeige();
        if (letzte_anzeige != 0) {
            time_t seit_boot_s = (time_t)(esp_timer_get_time() / 1000000);
            zeit_uebernehmen(letzte_anzeige + seit_boot_s);
        }
    }
    startbildschirm_schritt_fertig(schritt);
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
 * Reihenfolge "neuen Screen erst laden, dann alten erst loeschen".
 *
 * Rueckgabe true, wenn der Benutzer den Demo-Modus gewaehlt hat - die
 * Uhrzeit steht dann bereits auf dem festen Demo-Zeitstempel und der
 * Aufrufer soll alle noch offenen Boot-Phasen ueberspringen. */

/* Fester Zeitstempel fuer den Demo-Modus (Peters Wahl): Samstagabend kurz
 * vor der 18-Uhr-Tablette, damit eine Vorfuehrung ohne WLAN deterministisch
 * ablaeuft und binnen einer Minute "live" eine Tablette faellig wird. */
#define DEMO_TAG 18
#define DEMO_MONAT 7
#define DEMO_JAHR 2026
#define DEMO_STUNDE 17
#define DEMO_MINUTE 59

static bool einstellungen_bildschirm_verarbeiten(void)
{
    einrichtung_einstellungen_zeigen();
    for (;;) {
        einrichtung_status_t status = einrichtung_einstellungen_status();
        if (status != EINRICHTUNG_OFFEN)
            break;

        switch (einrichtung_einstellungen_aktion_abfragen()) {
        case EINSTELLUNGEN_AKTION_DEMO:
            ESP_LOGI(TAG, "Start: Demo-Modus gewaehlt - Uhrzeit auf %02d.%02d.%d %02d:%02d gesetzt, "
                          "restliche Boot-Phasen werden uebersprungen",
                     DEMO_TAG, DEMO_MONAT, DEMO_JAHR, DEMO_STUNDE, DEMO_MINUTE);
            zeit_manuell_setzen(DEMO_TAG, DEMO_MONAT, DEMO_JAHR, DEMO_STUNDE, DEMO_MINUTE);
            return true;
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
    return false;
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
        if (s_demo_modus) {
            startbildschirm_schritt_fertig(schritt);
            return;
        }

        phase_ergebnis_t ergebnis = phase_abwarten(schritt, bedingung);

        if (ergebnis == PHASE_FERTIG) {
            startbildschirm_schritt_fertig(schritt);
            return;
        }
        if (ergebnis == PHASE_TIMEOUT) {
            phase_timeout_automatisch_fortsetzen(schritt, name);
            return;
        }

        if (ergebnis == PHASE_EINSTELLUNGEN) {
            netz_watchdog_pausieren(true);
            s_demo_modus = einstellungen_bildschirm_verarbeiten();
            startbildschirm_reaktivieren();
            einrichtung_einstellungen_aufraeumen();
            netz_watchdog_pausieren(false);
            continue; /* Phase erneut abwarten (bzw. im Demo-Modus sofort ueberspringen) */
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
#if ENTWICKLUNGSWERKZEUGE
    screenshot_debug_start(); /* Entwicklungswerkzeug: Touch-Button unten Mitte -> Screenshot ueber seriell */
#endif
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

    /* Ab hier hat die Anzeige oberste Prioritaet - der WLAN-Watchdog darf
     * jetzt keinen Neustart mehr wegen normaler, kurzzeitiger
     * Verbindungsaussetzer ausloesen (siehe netz_watchdog_lockern). */
    netz_watchdog_lockern();

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
