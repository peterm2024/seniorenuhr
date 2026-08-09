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
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "kalender_anzeige.h"
#include "netz.h"
#include "ota.h"
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
/* Per #ifndef, damit der Release-Build den Wert ueber eine
 * Compiler-Definition auf 0 ziehen kann (siehe main/CMakeLists.txt und
 * .github/workflows/release.yml), OHNE diese Datei zu veraendern. Vorher
 * tat das ein "sed -i" im Workflow - dadurch galt der Arbeitsbaum als
 * veraendert und "git describe" lieferte "v0.9.0-dirty" statt "v0.9.0",
 * was die Zuordnung Version <-> Release-Tag zerstoerte. */
#ifndef ENTWICKLUNGSWERKZEUGE
#define ENTWICKLUNGSWERKZEUGE 1
#endif

/* Nach der letzten Beruehrung bleibt die Anzeige so lange im Tag-Modus,
 * bevor sie abends/nachts wieder abdunkelt. */
#define BERUEHRUNG_WACHZEIT_MS (30 * 1000)

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
#define FARBE_UPDATE_GRUEN      0x4caf50 /* Update-Symbol: eigenes Gruen statt des Akzent-Golds - faellt
                                           * deutlicher auf UND wirkt nicht wie eine Warnung (Peters
                                           * Wunsch, 09.08.2026: "grün, weil das nicht gefährlich aussieht") */
#define FARBE_VERGANGEN         0x707a8a /* gedaempftes Grau: vergangene Termine / abgehakte Tabletten in der Uebersicht */
#define FARBE_ZEIT_UNBESTAETIGT 0xcc7a1a /* dunkles Orange: grosse Uhrzeit blinkt damit, solange die Zeit nicht per NTP bestaetigt ist */
#define FARBE_TABLETTE_FAELLIG      FARBE_AKZENT /* faellig, noch unbestaetigt: dasselbe Gold wie Wochentag/Ueberschriften */
#define FARBE_TABLETTE_UEBERFAELLIG FARBE_WARNUNG /* seit KALENDER_TABLETTE_UEBERFAELLIG_MIN unbestaetigt: dasselbe Rot wie bei den Status-Symbolen */

/* Kantenlaenge der kleinen Status-Symbole rechts oben (WLAN/Zeit/Kalender). */
#define STATUS_ICON_GROESSE 34
#define STATUS_ICON_MAX_TEILE 4

/* WLAN/Zeit/Kalender stehen seit 09.08.2026 SENKRECHT uebereinander statt
 * nebeneinander (Peters Wunsch) - spart die Breite von zwei Symbolen
 * (vorher 650/700/750 nebeneinander) und gibt dem Wochentag-Titel mehr
 * Luft. Reihenfolge von oben nach unten entspricht der alten Reihenfolge
 * von links nach rechts (WLAN, Zeit, Kalender). */
#define STATUS_SPALTE_X      750 /* gemeinsame X-Position, wie vorher die rechte (Kalender-)Position */
#define STATUS_SPALTE_Y0       6 /* oberster Symbolrand */
#define STATUS_ICON_ABSTAND    6 /* Luecke zwischen zwei gestapelten Symbolen */
#define STATUS_SPALTE_SCHRITT (STATUS_ICON_GROESSE + STATUS_ICON_ABSTAND)
/* Tippflaechen-Hoehe fuer die Spalte: oberer Rand + 3 Symbole + 2 Luecken +
 * gleich grosser unterer Rand, grosszuegig gerundet. */
#define STATUS_SPALTE_HOEHE  (STATUS_SPALTE_Y0 + 3 * STATUS_ICON_GROESSE + 2 * STATUS_ICON_ABSTAND + STATUS_SPALTE_Y0)
/* Update-Symbol bleibt eigenstaendig LINKS der Spalte (Peters Wunsch: "das
 * spielt hier keine Rolle") statt mit hineingestapelt zu werden - auf
 * gleicher Hoehe wie das OBERSTE Symbol (WLAN), das wirkt aufgeraeumter als
 * mittig zur ganzen Spalte (per Screenshot verglichen, Peters Wahl).
 * STATUS_GRENZE_X trennt die beiden Tippflaechen ueberschneidungsfrei (wie
 * zuvor bei 640, siehe FALLSTRICKE #34-Umfeld). */
#define STATUS_UPDATE_X   660
#define STATUS_UPDATE_Y   STATUS_SPALTE_Y0
#define STATUS_GRENZE_X   700

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
    /* true fuer Zeilen mit einer ueberfaelligen, unbestaetigten Tablette -
     * nur bei der Tabletten-Spalte gesetzt. uhr_tick faerbt diese Zeilen
     * jede Sekunde direkt um (Blinken), ohne die Spalte neu aufzubauen. */
    bool ueberfaellig[UEBERSICHT_ZEILEN_MAX];
    int anzahl;
} uebersicht_spalte_t;

static uebersicht_spalte_t s_tabletten_spalte;
static uebersicht_spalte_t s_termine_spalte;
static lv_obj_t *s_dimm_overlay; /* nur fuer den Abend-Modus verwendet */

/* Wachzeit-Erkennung ueber LVGLs eigene Inaktivitaets-Uhr statt ueber einen
 * eigenen LV_EVENT_PRESSED-Callback auf dem Hauptbildschirm: jener bekam
 * Beruehrungen INNERHALB der Fenster (Checkboxen, OK/Abbrechen, Scrollen)
 * nie zu sehen, weil sie dort nicht bis zum Bildschirm durchbubbeln. Die
 * Wachzeit lief damit waehrend der Bedienung ab, und im Moment des
 * Fensterschliessens kippte die Anzeige uebergangslos in den Abend-/
 * Nachtmodus - von Peter als "geschieht meistens ueberraschend"
 * zurueckgemeldet. lv_display_get_inactive_time() zaehlt dagegen JEDE
 * Eingabe, unabhaengig davon welches Objekt sie entgegennimmt. */
static bool kuerzlich_beruehrt(void)
{
    return lv_display_get_inactive_time(NULL) < BERUEHRUNG_WACHZEIT_MS;
}

/* Tipp auf die Termine-Uebersicht oeffnet direkt das "Heute"-Fenster -
 * bisher ging das nur ueber den eigenen "Heute"-Button links. */
static void uebersicht_geklickt_cb(lv_event_t *e)
{
    (void)e;
    tagesansicht_heute_oeffnen();
}

/* Tipp auf die Tabletten-Uebersicht (Ausbaustufe 2, Punkt 6): oeffnet bei
 * mindestens einer gerade faelligen/ueberfaelligen, unbestaetigten Tablette
 * direkt die Erinnerungs-Checkliste statt des "Heute"-Fensters mit allen
 * Eintraegen. Derselbe Massstab wie tagesansicht_erinnerung_zeigen() selbst
 * anwendet (FAELLIG/UEBERFAELLIG) - sonst wuerde der Tipp bei einer erst
 * spaeter faelligen, noch unbestaetigten Tablette scheinbar ins Leere gehen
 * (die Checkliste zeigt in dem Fall naemlich nichts). Gibt es aktuell keine,
 * faellt der Tipp auf das gewohnte "Heute"-Fenster zurueck. */
static void tabletten_geklickt_cb(lv_event_t *e)
{
    (void)e;
    kalender_tag_eintrag_t eintraege[KALENDER_EINTRAEGE_MAX];
    int anzahl = kalender_anzeige_heutige_eintraege(eintraege, KALENDER_EINTRAEGE_MAX);

    bool etwas_faellig = false;
    if (zeit_ist_synchron()) {
        time_t jetzt = time(NULL);
        struct tm lokal;
        localtime_r(&jetzt, &lokal);
        int jetzt_minuten = lokal.tm_hour * 60 + lokal.tm_min;

        for (int i = 0; i < anzahl; i++) {
            if (!eintraege[i].ist_tablette)
                continue;
            kalender_tablette_status_t status = kalender_tablette_status(&eintraege[i], true, jetzt_minuten);
            if (status == KALENDER_TABLETTE_FAELLIG || status == KALENDER_TABLETTE_UEBERFAELLIG) {
                etwas_faellig = true;
                break;
            }
        }
    }

    if (etwas_faellig)
        tagesansicht_erinnerung_zeigen();
    else
        tagesansicht_heute_oeffnen();
}

/* Macht ein Label per Tipp klickbar (Ueberschrift/Inhalt der Tabletten-
 * bzw. Termine-Uebersicht) - Labels sind in LVGL standardmaessig nicht
 * klickbar. */
static void uebersicht_tippbar_machen(lv_obj_t *label, lv_event_cb_t klick_cb)
{
    lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(label, klick_cb, LV_EVENT_CLICKED, NULL);
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
     * ein Fenster darf nie in einen abgedunkelten Bildschirm hinein offen
     * stehen. Die Wachzeit danach zaehlt lv_display_get_inactive_time()
     * korrekt weiter (siehe kuerzlich_beruehrt), sodass es beim Schliessen
     * keinen abrupten Sprung mehr gibt. */
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

    if (modus != MODUS_TAG && kuerzlich_beruehrt())
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

/* Ausbaustufe 2 (Peter): die Tabletten-Spalte zeigt nicht mehr alle
 * heutigen Tabletten, sondern nur noch vorherige/aktuelle/naechste - genug
 * fuer einen Ueberblick, ohne dass viele Eintraege die Uebersicht sprengen.
 * "aktuell" ist die erste noch unbestaetigte (chronologisch, eintraege ist
 * bereits sortiert); sind alle bestaetigt, gilt die zeitlich letzte als
 * "aktuell" (bleibt sichtbar statt die Spalte leerzuraeumen). Schreibt bis
 * zu 3 Indizes nach positionen_aus, Rueckgabe: deren Anzahl (0, wenn heute
 * keine Tablette ansteht). */
static int tabletten_positionen_ermitteln(const kalender_tag_eintrag_t *eintraege, int anzahl,
                                           int *positionen_aus)
{
    int aktuell = -1;
    for (int i = 0; i < anzahl; i++) {
        if (!eintraege[i].ist_tablette)
            continue;
        aktuell = i;
        if (!eintraege[i].bestaetigt)
            break;
    }
    if (aktuell < 0)
        return 0;

    int vorherige = -1;
    for (int i = aktuell - 1; i >= 0; i--) {
        if (eintraege[i].ist_tablette) { vorherige = i; break; }
    }
    int naechste = -1;
    for (int i = aktuell + 1; i < anzahl; i++) {
        if (eintraege[i].ist_tablette) { naechste = i; break; }
    }

    int n = 0;
    if (vorherige >= 0) positionen_aus[n++] = vorherige;
    positionen_aus[n++] = aktuell;
    if (naechste >= 0) positionen_aus[n++] = naechste;
    return n;
}

/* True, wenn mindestens eine WEITERE Tablette exakt zur selben Uhrzeit
 * ansteht - dann zeigt die Zeile die Tageszeit statt eines der beiden Namen
 * (Peter: "bei mehreren gleichzeitigen Terminen statt des Namens die
 * Tageszeit anzeigen"). */
static bool tablette_zeit_kollidiert(const kalender_tag_eintrag_t *eintraege, int anzahl, int index)
{
    if (eintraege[index].ganztags)
        return false;
    int treffer = 0;
    for (int i = 0; i < anzahl; i++) {
        if (!eintraege[i].ist_tablette || eintraege[i].ganztags)
            continue;
        if (eintraege[i].stunde == eintraege[index].stunde && eintraege[i].minute == eintraege[index].minute)
            treffer++;
    }
    return treffer > 1;
}

/* zeit_tageszeit() (zeit.h) wertet nur tm_hour aus - hier mit der Soll-
 * Stunde der Tablette statt der aktuellen Uhrzeit aufgerufen. */
static const char *tageszeit_fuer_stunde(int stunde)
{
    struct tm hilfstm = {0};
    hilfstm.tm_hour = stunde;
    return zeit_tageszeit(&hilfstm);
}

/* Baut eine Fingerabdruck-Zeichenkette fuer eine der beiden Spalten
 * (Tabletten/Termine) aus den strukturierten Tageseintraegen - dient NUR
 * dem billigen Aenderungs-Check in uhr_tick() (strcmp gegen den vorherigen
 * Aufruf), nicht der direkten Anzeige. Die Faerbung wird als Farbcode mit
 * eincodiert, damit sich auch ein reiner Bestaetigt-/Vergangen-Wechsel
 * (ohne Textaenderung) zuverlaessig im Fingerabdruck niederschlaegt. Das
 * Blinken ueberfaelliger Tabletten laeuft bewusst NICHT hierueber (haette
 * einen kompletten Spalten-Neuaufbau pro Sekunde bedeutet, siehe
 * FALLSTRICKE #16 zum kleinen LVGL-Speicherpool) - stattdessen faerbt
 * uhr_tick() das gemerkte Label direkt um, analog zu zeit_blink_an. */
static void liste_text_aufbauen(const kalender_tag_eintrag_t *eintraege, int anzahl, bool nur_tabletten,
                                bool zeit_bekannt, int jetzt_minuten, char *ziel, size_t ziel_groesse)
{
    ziel[0] = '\0';
    size_t belegt = 0;
    int gefunden = 0;

    int positionen[UEBERSICHT_ZEILEN_MAX];
    int n_positionen;
    if (nur_tabletten) {
        n_positionen = tabletten_positionen_ermitteln(eintraege, anzahl, positionen);
    } else {
        n_positionen = 0;
        for (int i = 0; i < anzahl && n_positionen < UEBERSICHT_ZEILEN_MAX; i++)
            if (!eintraege[i].ist_tablette)
                positionen[n_positionen++] = i;
    }

    for (int k = 0; k < n_positionen; k++) {
        int i = positionen[k];
        gefunden++;

        /* Explizite Praezision (dynamisch aus der Zielgroesse) statt nacktem
         * "%s" - ueber einen Zeiger zugegriffene Array-Felder (titel)
         * verlieren bei GCCs Format-Truncation-Pruefung ihre bekannte
         * Groesse, siehe eintrag_zeile_formatieren (tagesansicht.c). */
        char inhalt[80];
        if (nur_tabletten && tablette_zeit_kollidiert(eintraege, anzahl, i))
            snprintf(inhalt, sizeof inhalt, "%02d:%02d  %s",
                     eintraege[i].stunde, eintraege[i].minute, tageszeit_fuer_stunde(eintraege[i].stunde));
        else if (eintraege[i].ganztags)
            snprintf(inhalt, sizeof inhalt, "%.*s", (int)sizeof inhalt - 1, eintraege[i].titel);
        else
            snprintf(inhalt, sizeof inhalt, "%02d:%02d  %.*s",
                     eintraege[i].stunde, eintraege[i].minute, (int)sizeof inhalt - 8, eintraege[i].titel);

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
    /* Verhindert, dass uhr_tick's Blink-Schleife anschliessend ein laengst
     * geloeschtes oder inzwischen andersartiges Label (z. B. den "..."-
     * Platzhalter) umfaerbt. */
    memset(spalte->ueberfaellig, 0, sizeof spalte->ueberfaellig);
}

/* Zeigt einen einzelnen Platzhaltertext an (z. B. "..." bevor Kalenderdaten
 * bekannt sind) - ersetzt alle bisherigen Zeilen. */
static void uebersicht_spalte_platzhalter_setzen(uebersicht_spalte_t *spalte, const char *text,
                                                  lv_event_cb_t klick_cb)
{
    uebersicht_spalte_leeren(spalte);
    lv_obj_t *label = lv_label_create(spalte->container);
    lv_obj_set_style_text_font(label, &schrift_klein_28, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(FARBE_TEXT_HELL), 0);
    lv_obj_set_pos(label, 0, 0);
    lv_label_set_text(label, text);
    uebersicht_tippbar_machen(label, klick_cb);
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

    int positionen[UEBERSICHT_ZEILEN_MAX];
    int n_positionen;
    if (nur_tabletten) {
        n_positionen = tabletten_positionen_ermitteln(eintraege, anzahl, positionen);
    } else {
        n_positionen = 0;
        for (int i = 0; i < anzahl && n_positionen < UEBERSICHT_ZEILEN_MAX; i++)
            if (!eintraege[i].ist_tablette)
                positionen[n_positionen++] = i;
    }

    int32_t y = 0;
    int gefunden = 0;
    for (int k = 0; k < n_positionen && spalte->anzahl < UEBERSICHT_ZEILEN_MAX; k++) {
        int i = positionen[k];
        gefunden++;

        bool abgehakt = nur_tabletten && eintraege[i].bestaetigt;
        bool vergangen = !nur_tabletten && zeit_bekannt && !eintraege[i].ganztags &&
                          (eintraege[i].stunde * 60 + eintraege[i].minute) < jetzt_minuten;
        bool ueberfaellig = false;

        uint32_t farbe = FARBE_TEXT_HELL;
        if (nur_tabletten) {
            switch (kalender_tablette_status(&eintraege[i], zeit_bekannt, jetzt_minuten)) {
            case KALENDER_TABLETTE_ABGEHAKT:     farbe = FARBE_VERGANGEN; break;
            case KALENDER_TABLETTE_FAELLIG:      farbe = FARBE_TABLETTE_FAELLIG; break;
            /* Startfarbe rot - der Sekunden-Blink-Abgleich in uhr_tick
             * uebernimmt ab dem naechsten Tick per spalte->ueberfaellig[]. */
            case KALENDER_TABLETTE_UEBERFAELLIG: farbe = FARBE_TABLETTE_UEBERFAELLIG; ueberfaellig = true; break;
            case KALENDER_TABLETTE_ZUKUNFT:      farbe = FARBE_TEXT_HELL; break;
            }
        } else if (vergangen) {
            farbe = FARBE_VERGANGEN;
        }

        /* Explizite Praezision (dynamisch aus der Zielgroesse) statt nacktem
         * "%s" - siehe Kommentar bei liste_text_aufbauen weiter oben. */
        char inhalt[88];
        const char *praefix = abgehakt ? UEBERSICHT_HAKEN_PRAEFIX : "";
        int praefix_laenge = (int)strlen(praefix);
        if (nur_tabletten && tablette_zeit_kollidiert(eintraege, anzahl, i))
            snprintf(inhalt, sizeof inhalt, "%s%02d:%02d  %s", praefix,
                     eintraege[i].stunde, eintraege[i].minute, tageszeit_fuer_stunde(eintraege[i].stunde));
        else if (eintraege[i].ganztags)
            snprintf(inhalt, sizeof inhalt, "%s%.*s", praefix,
                     (int)sizeof inhalt - 1 - praefix_laenge, eintraege[i].titel);
        else
            snprintf(inhalt, sizeof inhalt, "%s%02d:%02d  %.*s", praefix,
                     eintraege[i].stunde, eintraege[i].minute,
                     (int)sizeof inhalt - 8 - praefix_laenge, eintraege[i].titel);

        lv_obj_t *label = lv_label_create(spalte->container);
        lv_obj_set_style_text_font(label, &schrift_klein_28, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(farbe), 0);
        if (vergangen)
            lv_obj_set_style_text_decor(label, LV_TEXT_DECOR_STRIKETHROUGH, 0);
        /* Einzeilig mit "..." abschneiden statt umbrechen: Die Spalte ist nur
         * ~300px breit und der Platz nach unten knapp - ein Umbruch langer
         * Eintraege (z.B. "[x] 20:00  2,5x Abends") sprengte die feste
         * Zeilenhoehe, die Zeilen ueberlappten sich (bei den Eltern live
         * beobachtet, gleiche Fehlerklasse wie FALLSTRICKE #22). Feste
         * Label-Hoehe = Zeilenabstand -> nie Ueberlappung; der vollstaendige
         * Name bleibt im "Heute"-Fenster sichtbar. */
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_size(label, breite, UEBERSICHT_ZEILE_ABSTAND);
        lv_obj_set_pos(label, 0, y);
        lv_label_set_text(label, inhalt);
        uebersicht_tippbar_machen(label, nur_tabletten ? tabletten_geklickt_cb : uebersicht_geklickt_cb);

        spalte->ueberfaellig[spalte->anzahl] = ueberfaellig;
        spalte->zeilen[spalte->anzahl++] = label;
        y += UEBERSICHT_ZEILE_ABSTAND;
    }

    if (gefunden == 0 && spalte->anzahl < UEBERSICHT_ZEILEN_MAX) {
        lv_obj_t *label = lv_label_create(spalte->container);
        lv_obj_set_style_text_font(label, &schrift_klein_28, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(FARBE_TEXT_HELL), 0);
        lv_obj_set_pos(label, 0, 0);
        lv_label_set_text(label, "-");
        uebersicht_tippbar_machen(label, nur_tabletten ? tabletten_geklickt_cb : uebersicht_geklickt_cb);
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
/* Links VOR den drei Verbindungs-Symbolen; erscheint nur, wenn eine neue
 * Firmware bereitsteht (Peters Wunsch) - es ist also selbst schon die
 * Meldung, nicht bloss ein Zustandsanzeiger, und bleibt sonst unsichtbar. */
static status_icon_t s_status_update;
static lv_obj_t *s_update_tippflaeche;

/* Der Einstellungen-Task bekommt seinen Stack STATISCH, nicht aus dem Heap.
 *
 * Vorgeschichte, weil der Weg dahin zwei Sackgassen hatte: Zuerst fuhr
 * app_main() das Menue in einer Endlosschleife - damit blieb dessen 16-KB-
 * Stack fuer immer belegt und der OTA-Task bekam seine 8 KB nicht mehr.
 * Dann wurde der Task auf Zuruf erzeugt, also ausgerechnet in dem Moment,
 * in dem der interne SRAM am staerksten zerstueckelt ist: live scheiterten
 * nacheinander 12 KB (groesster freier Block 8704) und 8 KB (5632). Ein
 * Stack aus dem Heap ist hier schlicht eine Lotterie.
 *
 * Statisch heisst: der Platz steht schon beim Binden fest (.bss, interner
 * SRAM), kann nicht fragmentieren und kann nicht fehlschlagen. Weil er
 * ohnehin dauerhaft belegt ist, darf der Task auch dauerhaft leben - er
 * wartet blockierend auf ein Signal, statt sich immer wieder neu zu bilden.
 * Das erspart zugleich den heiklen Fall, dass ein zweiter Tipp den Task neu
 * anlegt, waehrend der alte noch abgeraeumt wird.
 *
 * Bezahlt wird das aus dem Kalender-Task, dessen 16 KB seit der Verlagerung
 * seines grossen ics_termin_t-Puffers in den PSRAM (FALLSTRICKE #26) weit
 * ueberdimensioniert waren - live gemessen 10060 Byte davon ungenutzt, jetzt
 * auf 10 KB gekuerzt. Unterm Strich gibt das Paar internen SRAM frei.
 *
 * Interner SRAM ist zwingend: der WLAN-Ablauf schreibt NVS, und waehrend
 * eines Flash-Zugriffs ist der Cache eingefroren, PSRAM also unerreichbar
 * (siehe ota.c). */
#define EINSTELLUNGEN_TASK_STACK_BYTES 8192

static StackType_t s_einstellungen_stack[EINSTELLUNGEN_TASK_STACK_BYTES / sizeof(StackType_t)];
static StaticTask_t s_einstellungen_tcb;
static TaskHandle_t s_einstellungen_task_handle;
/* Verhindert, dass ein zweiter Tipp waehrend des offenen Menues ein zweites
 * Oeffnen nachlegt (die Benachrichtigung wuerde sonst gezaehlt und der
 * Ablauf liefe direkt nochmal). */
static volatile bool s_einstellungen_offen = false;

static void uhr_tick(lv_timer_t *timer);
static bool einstellungen_bildschirm_verarbeiten(void);

/* Das Einstellungen-Menue ist ein blockierender Ablauf und darf deshalb
 * nicht im LVGL-Callback laufen (Task-Watchdog, FALLSTRICKE #16). */
static void einstellungen_task(void *arg)
{
    (void)arg;
    for (;;) {
        /* Nicht unbegrenzt blockieren: derselbe Task erledigt nebenbei die
         * aufgeschobene Netz-Arbeit. Sie faellt im WLAN-Ereignis-Handler an,
         * darf dort aber nicht ausgefuehrt werden (Task "sys_evt" hat nur
         * 2304 Byte Stack - siehe netz.h). Hier stehen 8 KB zur Verfuegung,
         * und der Task tut sonst nichts. */
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000)) == 0) {
            netz_wartung_ausfuehren();
            continue;
        }
        s_einstellungen_offen = true;

        netz_watchdog_pausieren(true);
        (void)einstellungen_bildschirm_verarbeiten(); /* Demo-Modus hier ohne Belang */
        lvgl_port_lock(0);
        lv_screen_load(s_bildschirm); /* zurueck zur Uhr */
        lvgl_port_unlock();
        einrichtung_einstellungen_aufraeumen();
        netz_watchdog_pausieren(false);
        /* Anzeige sofort auffrischen statt bis zum naechsten Sekundentakt zu
         * warten. uhr_tick ruehrt LVGL an und laeuft sonst im LVGL-Task -
         * hier also unter dessen Sperre (rekursiv, verschachtelt gefahrlos). */
        lvgl_port_lock(0);
        uhr_tick(NULL);
        lvgl_port_unlock();

        ESP_LOGI(TAG, "Einstellungen-Menue geschlossen (ungenutzte Stack-Reserve: %u von %u Byte)",
                 (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)),
                 (unsigned)EINSTELLUNGEN_TASK_STACK_BYTES);
        s_einstellungen_offen = false;
    }
}

static void einstellungen_task_starten(void)
{
    s_einstellungen_task_handle =
        xTaskCreateStatic(einstellungen_task, "einstellungen",
                          sizeof s_einstellungen_stack / sizeof s_einstellungen_stack[0],
                          NULL, 4, s_einstellungen_stack, &s_einstellungen_tcb);
}

/* Der Weg ins Einstellungen-Menue ist bewusst schwer zu treffen.
 *
 * Die Eltern sollen dieses Menue NIE zu Gesicht bekommen - es ueberfordert
 * sie vollstaendig. Das Update-Symbol ist eine Meldung an Peter ("hier gibt es
 * etwas Neues"), kein Bedienelement fuer sie. Ein normaler Tipp war deshalb
 * eine Falltuer: ein versehentlicher Treffer, und sie stehen mitten in den
 * Einstellungen.
 *
 * Statt das Symbol ganz zu sperren (dann kaeme Peter nur noch ueber einen
 * Neustart und das Zahnrad des Startbildschirms hinein) braucht es zwei
 * bewusste Handlungen: fuenf Sekunden gedrueckt halten, danach im Dialog
 * bestaetigen. Fuenf Sekunden haelt niemand versehentlich den Finger drauf.
 *
 * LVGLs eigenes LV_EVENT_LONG_PRESSED taugt hier nicht: dessen Dauer haengt
 * am Eingabegeraet (lv_indev_set_long_press_time) und gilt damit fuer ALLE
 * Bedienelemente - eine Umstellung auf 5 s wuerde jeden anderen langen Druck
 * im Programm mitverbiegen. Deshalb hier von Hand gemessen. */
/* Eigene Farben fuer den Bestaetigungsdialog - dieselben Toene wie die
 * OK-/Abbrechen-Knoepfe der Tabletten-Fenster (tagesansicht.c), damit sich
 * die Bedienung ueberall gleich anfuehlt. */
#define FARBE_DIALOG_JA   0x2e7d32 /* Gruen: bestaetigen */
#define FARBE_DIALOG_NEIN 0x4a5568 /* gedaempftes Grau: abbrechen */

#define UPDATE_HALTEDAUER_MS 5000
/* Der Bestaetigungsdialog verschwindet von selbst wieder - sollte er doch
 * einmal ungewollt erscheinen, bleibt er nicht dauerhaft im Weg. */
#define UPDATE_DIALOG_ANZEIGEDAUER_MS 20000

static uint32_t s_update_druck_beginn;
static bool s_update_druck_ausgeloest;
static lv_obj_t *s_update_dialog;
static lv_timer_t *s_update_dialog_timer;

static void update_dialog_schliessen(void)
{
    if (s_update_dialog_timer) {
        lv_timer_delete(s_update_dialog_timer);
        s_update_dialog_timer = NULL;
    }
    if (s_update_dialog) {
        lv_obj_delete(s_update_dialog);
        s_update_dialog = NULL;
    }
}

static void update_dialog_timer_cb(lv_timer_t *t)
{
    (void)t;
    s_update_dialog_timer = NULL; /* laeuft gerade ab, nicht noch einmal loeschen */
    update_dialog_schliessen();
}

static void update_dialog_abbrechen_cb(lv_event_t *e)
{
    (void)e;
    update_dialog_schliessen();
}

static void update_dialog_oeffnen_cb(lv_event_t *e)
{
    (void)e;
    update_dialog_schliessen();
    if (!s_einstellungen_offen && s_einstellungen_task_handle)
        xTaskNotifyGive(s_einstellungen_task_handle);
}

static lv_obj_t *dialog_button_erzeugen(lv_obj_t *parent, const char *text, uint32_t farbe,
                                         lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_height(btn, 64);
    lv_obj_set_style_min_width(btn, 180, 0);
    lv_obj_set_width(btn, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(btn, 20, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(farbe), 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &schrift_klein_28, 0);
    lv_obj_center(label);
    return btn;
}

static void update_dialog_zeigen(void)
{
    if (s_update_dialog)
        return;

    /* Auf lv_layer_top(), damit der Dialog unabhaengig vom gerade geladenen
     * Bildschirm sichtbar ist - denselben Fehler hatte das Fortschritts-
     * fenster schon einmal (FALLSTRICKE #34). */
    s_update_dialog = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_update_dialog, 560, 260);
    lv_obj_center(s_update_dialog);
    lv_obj_remove_flag(s_update_dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_update_dialog, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_update_dialog, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_update_dialog, lv_color_hex(FARBE_AKZENT), 0);
    lv_obj_set_style_border_width(s_update_dialog, 2, 0);
    lv_obj_set_style_radius(s_update_dialog, 12, 0);

    lv_obj_t *titel = lv_label_create(s_update_dialog);
    lv_label_set_text(titel, "Einstellungen oeffnen?");
    lv_obj_set_style_text_font(titel, &schrift_mittel_40, 0);
    lv_obj_set_style_text_color(titel, lv_color_hex(FARBE_AKZENT), 0);
    lv_obj_align(titel, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *hinweis = lv_label_create(s_update_dialog);
    lv_label_set_text(hinweis, "Nur fuer die Wartung gedacht.");
    lv_label_set_long_mode(hinweis, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hinweis, lv_pct(100));
    lv_obj_set_style_text_align(hinweis, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(hinweis, &schrift_klein_28, 0);
    lv_obj_set_style_text_color(hinweis, lv_color_hex(FARBE_TEXT_HELL), 0);
    lv_obj_align(hinweis, LV_ALIGN_TOP_MID, 0, 75);

    /* Abbrechen links, Oeffnen rechts - dieselbe Anordnung wie bei
     * OK/Abbrechen in den Tabletten-Fenstern, damit die Bedienung ueberall
     * gleich funktioniert. */
    lv_obj_t *abbrechen = dialog_button_erzeugen(s_update_dialog, "Abbrechen", FARBE_DIALOG_NEIN,
                                                  update_dialog_abbrechen_cb);
    lv_obj_align(abbrechen, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *oeffnen = dialog_button_erzeugen(s_update_dialog, "Oeffnen", FARBE_DIALOG_JA,
                                                update_dialog_oeffnen_cb);
    lv_obj_align(oeffnen, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    s_update_dialog_timer = lv_timer_create(update_dialog_timer_cb, UPDATE_DIALOG_ANZEIGEDAUER_MS, NULL);
    lv_timer_set_repeat_count(s_update_dialog_timer, 1);
}

static void update_symbol_geklickt_cb(lv_event_t *e)
{
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        s_update_druck_beginn = lv_tick_get();
        s_update_druck_ausgeloest = false;
        break;
    case LV_EVENT_PRESSING:
        if (!s_update_druck_ausgeloest &&
            lv_tick_elaps(s_update_druck_beginn) >= UPDATE_HALTEDAUER_MS) {
            s_update_druck_ausgeloest = true; /* nur einmal pro Druck */
            update_dialog_zeigen();
        }
        break;
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
        s_update_druck_ausgeloest = false;
        break;
    default:
        break;
    }
}
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

static void status_icon_erzeugen(status_icon_t *icon, lv_obj_t *scr, int32_t x, int32_t y)
{
    icon->glyph_anzahl = 0;

    icon->container = lv_obj_create(scr);
    lv_obj_remove_style_all(icon->container);
    lv_obj_set_size(icon->container, STATUS_ICON_GROESSE, STATUS_ICON_GROESSE);
    lv_obj_set_pos(icon->container, x, y);
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
/* Pfeil nach unten auf eine Grundlinie - das gelaeufige "Herunterladen/
 * Aktualisieren"-Zeichen. Bewusst dieselbe Ring-Optik wie die drei
 * Verbindungs-Symbole, damit es sich in die Reihe einfuegt statt als
 * Fremdkoerper zu wirken. */
static void status_glyph_update_erzeugen(status_icon_t *icon)
{
    static const lv_point_precise_t schaft[2] = {{17, 8}, {17, 21}};
    lv_obj_t *linie = lv_line_create(icon->container);
    lv_line_set_points(linie, schaft, 2);
    lv_obj_set_style_line_width(linie, 3, 0);
    lv_obj_set_style_line_rounded(linie, true, 0);
    status_icon_teil_hinzufuegen(icon, linie);

    static const lv_point_precise_t spitze[3] = {{11, 15}, {17, 22}, {23, 15}};
    lv_obj_t *pfeil = lv_line_create(icon->container);
    lv_line_set_points(pfeil, spitze, 3);
    lv_obj_set_style_line_width(pfeil, 3, 0);
    lv_obj_set_style_line_rounded(pfeil, true, 0);
    status_icon_teil_hinzufuegen(icon, pfeil);

    static const lv_point_precise_t boden[2] = {{10, 26}, {24, 26}};
    lv_obj_t *grundlinie = lv_line_create(icon->container);
    lv_line_set_points(grundlinie, boden, 2);
    lv_obj_set_style_line_width(grundlinie, 3, 0);
    lv_obj_set_style_line_rounded(grundlinie, true, 0);
    status_icon_teil_hinzufuegen(icon, grundlinie);
}

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
    uebersicht_tippbar_machen(s_tabletten_ueberschrift, tabletten_geklickt_cb);

    s_termine_ueberschrift = ueberschrift_erzeugen(s_bildschirm, "TERMINE HEUTE", 420, 335, 300);
    s_termine_spalte.container = uebersicht_container_erzeugen(s_bildschirm, 420, 385, UEBERSICHT_SPALTE_BREITE);
    uebersicht_tippbar_machen(s_termine_ueberschrift, uebersicht_geklickt_cb);

    /* Wochentag-Navigation: 7 Buttons links (gestern..+5 Tage) + Heute-
     * Button rechts, oeffnen Tages-/Heute-Fenster mit Terminen/Tabletten. */
    tagesansicht_erstellen(s_bildschirm);

    /* Update-Hinweis links vor der Status-Spalte - startet unsichtbar und
     * erscheint erst, wenn eine neue Firmware bereitsteht (siehe uhr_tick).
     * Kein Durchstrich: es gibt hier kein "kaputt", das Symbol ist entweder
     * da oder nicht. Eigene Gruen-Farbe statt des Akzent-Golds (siehe
     * FARBE_UPDATE_GRUEN) - faellt so deutlicher auf und wirkt nicht wie
     * eine Warnung (Peters Wunsch). */
    status_icon_erzeugen(&s_status_update, s_bildschirm, STATUS_UPDATE_X, STATUS_UPDATE_Y);
    status_glyph_update_erzeugen(&s_status_update);
    lv_obj_add_flag(s_status_update.container, LV_OBJ_FLAG_HIDDEN);

    /* Eigene, grosszuegige Tippflaeche wie bei den Status-Symbolen: das
     * Symbol ist zugleich der Weg zum Update. Ohne das waere es eine
     * Sackgasse - das Einstellungen-Menue haengt sonst am Zahnrad des
     * STARTbildschirms und ist nach dem Booten gar nicht mehr erreichbar,
     * das Symbol erschiene also genau dann, wenn man nichts mehr damit
     * anfangen kann. Wird zusammen mit dem Symbol ein-/ausgeblendet. Reicht
     * ueber die volle Spaltenhoehe (nicht nur bis zum Symbol selbst) - ein
     * grosszuegiges Ziel fuer zitternde/ungenaue Haende. */
    s_update_tippflaeche = lv_obj_create(s_bildschirm);
    lv_obj_remove_style_all(s_update_tippflaeche);
    lv_obj_set_pos(s_update_tippflaeche, 610, 0);
    lv_obj_set_size(s_update_tippflaeche, STATUS_GRENZE_X - 610, STATUS_SPALTE_HOEHE);
    lv_obj_add_flag(s_update_tippflaeche, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_update_tippflaeche, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_update_tippflaeche, LV_OBJ_FLAG_HIDDEN);
    /* ALLE Ereignisse, nicht nur LV_EVENT_CLICKED: die Haltedauer wird selbst
     * gemessen (siehe update_symbol_geklickt_cb). Ein kurzer Tipp bewirkt
     * bewusst nichts. */
    lv_obj_add_event_cb(s_update_tippflaeche, update_symbol_geklickt_cb, LV_EVENT_ALL, NULL);

    /* Live-Status rechts oben: spiegelt WLAN/Zeit/Kalender aus dem
     * Startbildschirm, durchgestrichen bei fehlender Konnektivitaet. Seit
     * 09.08.2026 SENKRECHT gestapelt statt nebeneinander (Peters Wunsch,
     * siehe STATUS_SPALTE_* oben) - spart Breite fuer den Wochentag-Titel. */
    status_icon_erzeugen(&s_status_wlan, s_bildschirm, STATUS_SPALTE_X, STATUS_SPALTE_Y0);
    status_glyph_wlan_erzeugen(&s_status_wlan);
    status_icon_durchstrich_erzeugen(&s_status_wlan);

    status_icon_erzeugen(&s_status_zeit, s_bildschirm, STATUS_SPALTE_X, STATUS_SPALTE_Y0 + STATUS_SPALTE_SCHRITT);
    status_glyph_zeit_erzeugen(&s_status_zeit);
    status_icon_durchstrich_erzeugen(&s_status_zeit);

    status_icon_erzeugen(&s_status_kalender, s_bildschirm, STATUS_SPALTE_X, STATUS_SPALTE_Y0 + 2 * STATUS_SPALTE_SCHRITT);
    status_glyph_kalender_erzeugen(&s_status_kalender);
    status_icon_durchstrich_erzeugen(&s_status_kalender);

    /* Unsichtbare, grosszuegige Tippflaeche ueber allen drei Status-
     * Symbolen (Peters Idee) - oeffnet das Status-Detail-Fenster. Bewusst
     * deutlich groesser als die 34px-Symbole selbst (seniorengerechtes,
     * leicht zu treffendes Ziel), reicht bis zur rechten Bildschirmkante,
     * wo sonst nichts anderes liegt, und ueber die volle Spaltenhoehe. */
    lv_obj_t *status_tippflaeche = lv_obj_create(s_bildschirm);
    lv_obj_remove_style_all(status_tippflaeche);
    /* Beginnt bei STATUS_GRENZE_X, damit die beiden Tippflaechen luecken-
     * und ueberschneidungsfrei aneinander liegen. */
    lv_obj_set_pos(status_tippflaeche, STATUS_GRENZE_X, 0);
    lv_obj_set_size(status_tippflaeche, 800 - STATUS_GRENZE_X, STATUS_SPALTE_HOEHE);
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
    /* Update-Symbol in eigenem Gruen statt im Grau der Verbindungs-Symbole:
     * es ist eine Aufforderung ("ins Menue gehen"), kein Dauerzustand. */
    status_icon_farbe_setzen(&s_status_update, lv_color_hex(FARBE_UPDATE_GRUEN));
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
    /* Sichtbarkeit des Update-Symbols bewusst NICHT hier, sondern im
     * Sekunden-Tick (uhr_tick) - diese Funktion laeuft nur bei
     * Moduswechseln und wuerde ein neu eintreffendes Update zu spaet
     * anzeigen. */

    lvgl_port_unlock();
}

/* Erinnerungsfenster (siehe tagesansicht_erinnerung_zeigen): Reagiert
 * niemand, meldet es sich in diesem Abstand erneut - hoechstens
 * ERINNERUNG_MAX_VERSUCHE mal, danach uebernimmt die rote Faerbung in der
 * Uebersicht als bleibender Hinweis (kein endloses Nagen). */
#define ERINNERUNG_WIEDERHOLUNG_US (10LL * 60 * 1000000)
#define ERINNERUNG_MAX_VERSUCHE    5

/* Fingerprint-Baustein pro Tablette (Sollzeit+Titel) - ICS_TITEL_MAX aus
 * ics_parser.h (ueber kalender_anzeige.h eingebunden). */
#define ERINNERUNG_FINGERPRINT_STUECK_MAX (ICS_TITEL_MAX + 16)

/* Prueft einmal pro Sekunden-Tick, welche Tabletten gerade faellig oder
 * ueberfaellig und noch nicht abgehakt sind, und laesst dafuer die
 * Erinnerungs-Checkliste aufpoppen (Ausbaustufe 2: zeigt alle auf einmal,
 * nicht mehr nur die erste). Ein Fingerprint aus Sollzeit+Titel der
 * gesamten Menge (gleiches Prinzip wie liste_text_aufbauen) erkennt jede
 * Aenderung - neue Tablette faellig, oder eine bestaetigt und faellt raus -
 * und setzt dann den Wiederholungs-Zaehler zurueck. Bewusst NICHT nachts
 * (Peters Wunsch: der Bildschirm bleibt zwischen 22:00 und 6:00 dunkel) und
 * nicht, waehrend ohnehin schon ein Fenster offen ist - ein Popup darf
 * niemandem mitten in die Bedienung springen. */
static void erinnerung_pruefen(const kalender_tag_eintrag_t *eintraege, int anzahl,
                                int jetzt_minuten, anzeige_modus_t modus)
{
    static char s_fingerprint[KALENDER_EINTRAEGE_MAX * ERINNERUNG_FINGERPRINT_STUECK_MAX] = "";
    static int s_versuche = 0;
    static int64_t s_naechster_us = 0;

    if (modus == MODUS_NACHT || tagesansicht_fenster_offen() || ota_laeuft())
        return;

    char neuer_fingerprint[sizeof s_fingerprint];
    neuer_fingerprint[0] = '\0';
    size_t belegt = 0;
    bool etwas_faellig = false;
    for (int i = 0; i < anzahl; i++) {
        if (!eintraege[i].ist_tablette)
            continue;
        kalender_tablette_status_t status = kalender_tablette_status(&eintraege[i], true, jetzt_minuten);
        if (status != KALENDER_TABLETTE_FAELLIG && status != KALENDER_TABLETTE_UEBERFAELLIG)
            continue;
        etwas_faellig = true;

        char stueck[ERINNERUNG_FINGERPRINT_STUECK_MAX];
        snprintf(stueck, sizeof stueck, "%02d:%02d %.*s|", eintraege[i].stunde, eintraege[i].minute,
                 (int)sizeof stueck - 8, eintraege[i].titel);
        size_t n = strlen(stueck);
        if (belegt + n < sizeof neuer_fingerprint) {
            memcpy(neuer_fingerprint + belegt, stueck, n);
            belegt += n;
            neuer_fingerprint[belegt] = '\0';
        }
    }

    if (!etwas_faellig) {
        /* Nichts (mehr) faellig - die naechste Tablette faengt mit frischem
         * Zaehler an. Das setzt den Zaehler auch ueber Nacht zurueck, sodass
         * dieselbe Tablette morgen wieder alle Versuche bekommt. */
        s_fingerprint[0] = '\0';
        return;
    }

    if (strcmp(neuer_fingerprint, s_fingerprint) != 0) {
        snprintf(s_fingerprint, sizeof s_fingerprint, "%s", neuer_fingerprint);
        s_versuche = 0;
        s_naechster_us = 0;
    }
    if (s_versuche >= ERINNERUNG_MAX_VERSUCHE)
        return;

    int64_t jetzt_us = esp_timer_get_time();
    if (jetzt_us < s_naechster_us)
        return;

    s_versuche++;
    s_naechster_us = jetzt_us + ERINNERUNG_WIEDERHOLUNG_US;
    ESP_LOGI(TAG, "Tabletten-Erinnerung %d/%d", s_versuche, ERINNERUNG_MAX_VERSUCHE);
    tagesansicht_erinnerung_zeigen();
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

    /* Blink-Zustand fuer ueberfaellige, unbestaetigte Tabletten in der
     * Tabletten-Spalte (Ausbaustufe 2) - toggelt jede Sekunde bedingungslos
     * (billig, ein Bool), analog zu zeit_blink_an oben. Faerbt die
     * betroffenen Zeilen-Labels DIREKT um (spalte->ueberfaellig[], gesetzt
     * von uebersicht_spalte_neu_aufbauen) statt die Spalte neu aufzubauen -
     * ein kompletter Delete/Create-Zyklus pro Sekunde waere unnoetiger
     * Verschleiss des kleinen, festen LVGL-Speicherpools (FALLSTRICKE #16). */
    static bool tablette_blink_an = false;
    tablette_blink_an = !tablette_blink_an;
    lvgl_port_lock(0);
    for (int k = 0; k < s_tabletten_spalte.anzahl; k++) {
        if (s_tabletten_spalte.ueberfaellig[k])
            lv_obj_set_style_text_color(s_tabletten_spalte.zeilen[k],
                lv_color_hex(tablette_blink_an ? FARBE_TABLETTE_UEBERFAELLIG : FARBE_TEXT_HELL), 0);
    }
    lvgl_port_unlock();

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

    /* Update-Symbol hier statt in modus_anwenden pruefen: jene Funktion
     * laeuft nur bei einem MODUSwechsel, ein waehrend des Tages
     * eintreffendes Update wuerde sonst erst Stunden spaeter sichtbar. */
    static bool letztes_update_sichtbar = false;
    bool update_sichtbar = (modus != MODUS_NACHT) && ota_update_verfuegbar();
    if (einmalig || update_sichtbar != letztes_update_sichtbar) {
        if (update_sichtbar) {
            lv_obj_remove_flag(s_status_update.container, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(s_update_tippflaeche, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_status_update.container, LV_OBJ_FLAG_HIDDEN);
            /* Tippflaeche mit ausblenden - eine unsichtbare, aber aktive
             * Flaeche waere eine Falle: ein Tipp neben die Uhr wuerde
             * unerklaerlich ins Einstellungen-Menue springen. */
            lv_obj_add_flag(s_update_tippflaeche, LV_OBJ_FLAG_HIDDEN);
        }
        letztes_update_sichtbar = update_sichtbar;
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

    /* Muss VOR dem Aenderungs-Vergleich stehen: unten wird bei unveraendertem
     * Text frueh zurueckgesprungen, und genau dann (die Uhr laeuft, sonst
     * passiert nichts) soll die Erinnerung ja gerade greifen. */
    if (hat_daten && zeit_bekannt)
        erinnerung_pruefen(eintraege, anzahl, jetzt_minuten, modus);

    /* Update-Hinweisfenster: folgt dem ota_laeuft()-Zustand unabhaengig
     * davon, ob sich sonst an der Anzeige etwas geaendert hat - deshalb
     * ebenfalls hier, noch vor dem fruehen Ausstieg unten. */
    static bool ota_fenster_war_offen = false;
    bool ota_aktiv = ota_laeuft();
    if (ota_aktiv) {
        if (!ota_fenster_war_offen)
            tagesansicht_update_fenster_zeigen();
        /* Solange etwas im Klartext zu sagen ist (Verbindungsaufbau,
         * Fehlschlag), hat das Vorrang vor dem Prozentbalken - beim
         * Verbindungsaufbau gibt es noch gar keinen Fortschritt. */
        const char *meldung = ota_meldung();
        if (meldung[0])
            tagesansicht_update_fenster_meldung_setzen(meldung);
        else
            tagesansicht_update_fenster_fortschritt_setzen(ota_fortschritt_prozent());
    } else if (ota_fenster_war_offen) {
        tagesansicht_update_fenster_schliessen();
    }
    ota_fenster_war_offen = ota_aktiv;

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
            uebersicht_spalte_platzhalter_setzen(&s_tabletten_spalte, "...", tabletten_geklickt_cb);
    }
    if (termine_geaendert) {
        if (hat_daten)
            uebersicht_spalte_neu_aufbauen(&s_termine_spalte, UEBERSICHT_SPALTE_BREITE,
                                            eintraege, anzahl, false, zeit_bekannt, jetzt_minuten);
        else
            uebersicht_spalte_platzhalter_setzen(&s_termine_spalte, "...", uebersicht_geklickt_cb);
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
        case EINSTELLUNGEN_AKTION_UPDATE:
            /* Nur anstossen - der Download laeuft im OTA-Hintergrund-Task
             * und startet das Geraet danach selbst neu. Das Menue bleibt
             * offen; das Fortschrittsfenster erscheint ueber uhr_tick,
             * sobald der Download tatsaechlich losgelaufen ist. */
            ESP_LOGI(TAG, "Update im Einstellungen-Menue angestossen (auf %s)", ota_verfuegbare_version());
            ota_installation_anstossen();
            break;
        case EINSTELLUNGEN_AKTION_VERSION_WAEHLEN: {
            const char *gewaehlt = einrichtung_einstellungen_gewaehlte_version();
            ESP_LOGI(TAG, "Version %s aus der Auswahlliste angefordert (laufend: %s)",
                     gewaehlt, ota_laufende_version());
            ota_version_installieren(gewaehlt);
            break;
        }
        case EINSTELLUNGEN_AKTION_VERSION_ZURUECK:
            if (ota_auf_vorherige_version_wechseln() == ESP_OK) {
                ESP_LOGI(TAG, "Zurueck auf vorherige Version - Neustart");
                vTaskDelay(pdMS_TO_TICKS(500)); /* Log noch rausschreiben lassen */
                esp_restart();
            }
            break;
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

/* True nur bei einem echten Absturz (kein normaler Stromausfall/Neustart) -
 * genau diese Faelle sollen beim naechsten Start die Diagnose-Meldung zeigen
 * (Peters Wunsch, Beta-Phase). ESP_RST_SW (WLAN-Watchdog, Speichern im
 * Einrichtungsbildschirm) und POWERON/EXT bleiben bewusst aussen vor. */
static bool reset_ist_absturz(esp_reset_reason_t grund)
{
    return grund == ESP_RST_PANIC || grund == ESP_RST_TASK_WDT ||
           grund == ESP_RST_INT_WDT || grund == ESP_RST_WDT ||
           grund == ESP_RST_BROWNOUT;
}

/* Kurzer, laienverstaendlicher Text fuer die Absturz-Diagnose auf dem Geraet
 * (der ausfuehrliche reset_grund_text() geht ins serielle Log). */
static const char *reset_grund_kurz(esp_reset_reason_t grund)
{
    switch (grund) {
    case ESP_RST_PANIC:    return "Programmabsturz";
    case ESP_RST_TASK_WDT: return "Haenger (Task-Watchdog)";
    case ESP_RST_INT_WDT:  return "Haenger (Interrupt-Watchdog)";
    case ESP_RST_WDT:      return "Watchdog";
    case ESP_RST_BROWNOUT: return "Unterspannung (Netzteil?)";
    default:               return "unbekannt";
    }
}

static volatile bool s_diagnose_bestaetigt;

static void diagnose_ok_cb(lv_event_t *e)
{
    (void)e;
    s_diagnose_bestaetigt = true;
}

/* Blackbox-Meldung nach einem echten Absturz: zeigt Grund, ungefaehren
 * Absturzzeitpunkt (aus dem 60s-Heartbeat einstellungen_letzte_anzeige) und
 * die laufende Absturznummer - zum Abfotografieren. Blockiert app_main()
 * BIS zur Bestaetigung per Touch. Die unbegrenzte Blockade ist eine bewusste
 * Beta-Entscheidung (Peter: keine Absturz-Info verlieren) - fuer den
 * spaeteren Produktivbetrieb sollte hier ein Sicherheits-Timeout ergaenzt
 * werden, damit die normale Anzeige (Tabletten!) nachts nicht dauerhaft
 * verdeckt bleibt. Der WLAN-Watchdog laeuft zu diesem Zeitpunkt noch nicht
 * (netz_start kommt spaeter), ein Blockieren ist hier also gefahrlos. */
static void diagnose_screen_zeigen_und_warten(esp_reset_reason_t grund)
{
    s_diagnose_bestaetigt = false;

    char zeit_txt[48];
    time_t letzte = einstellungen_letzte_anzeige();
    if (letzte > 0) {
        struct tm tm_letzte;
        localtime_r(&letzte, &tm_letzte);
        snprintf(zeit_txt, sizeof zeit_txt, "%02d.%02d.%04d, %02d:%02d Uhr",
                 tm_letzte.tm_mday, tm_letzte.tm_mon + 1, tm_letzte.tm_year + 1900,
                 tm_letzte.tm_hour, tm_letzte.tm_min);
    } else {
        snprintf(zeit_txt, sizeof zeit_txt, "unbekannt (Zeit nie gesetzt)");
    }

    char grund_txt[64];
    snprintf(grund_txt, sizeof grund_txt, "Grund: %s", reset_grund_kurz(grund));
    char zeit_zeile[80];
    snprintf(zeit_zeile, sizeof zeit_zeile, "Zuletzt aktiv: %s", zeit_txt);
    char nummer_zeile[48];
    snprintf(nummer_zeile, sizeof nummer_zeile, "Absturz Nr. %lu",
             (unsigned long)einstellungen_absturz_zaehler());

    lvgl_port_lock(0);
    lv_obj_t *vorher = lv_screen_active();

    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(FARBE_TAG_HINTERGRUND), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *titel = lv_label_create(screen);
    lv_label_set_text(titel, "NEUSTART NACH FEHLER");
    lv_obj_set_style_text_font(titel, &schrift_mittel_40, 0);
    lv_obj_set_style_text_color(titel, lv_color_hex(FARBE_WARNUNG), 0);
    lv_obj_align(titel, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *g = lv_label_create(screen);
    lv_label_set_text(g, grund_txt);
    lv_obj_set_style_text_font(g, &schrift_klein_28, 0);
    lv_obj_set_style_text_color(g, lv_color_hex(FARBE_TEXT_HELL), 0);
    lv_obj_align(g, LV_ALIGN_TOP_MID, 0, 130);

    lv_obj_t *z = lv_label_create(screen);
    lv_label_set_text(z, zeit_zeile);
    lv_obj_set_style_text_font(z, &schrift_klein_28, 0);
    lv_obj_set_style_text_color(z, lv_color_hex(FARBE_TEXT_HELL), 0);
    lv_obj_align(z, LV_ALIGN_TOP_MID, 0, 180);

    lv_obj_t *n = lv_label_create(screen);
    lv_label_set_text(n, nummer_zeile);
    lv_obj_set_style_text_font(n, &schrift_klein_28, 0);
    lv_obj_set_style_text_color(n, lv_color_hex(FARBE_TEXT_HELL), 0);
    lv_obj_align(n, LV_ALIGN_TOP_MID, 0, 230);

    lv_obj_t *hinweis = lv_label_create(screen);
    lv_label_set_text(hinweis, "Bitte abfotografieren, dann bestaetigen.");
    lv_obj_set_style_text_font(hinweis, &schrift_klein_28, 0);
    lv_obj_set_style_text_color(hinweis, lv_color_hex(FARBE_STATUS_HELL), 0);
    lv_obj_align(hinweis, LV_ALIGN_TOP_MID, 0, 290);

    lv_obj_t *btn = lv_button_create(screen);
    lv_obj_set_size(btn, 320, 72);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_add_event_cb(btn, diagnose_ok_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Verstanden");
    lv_obj_set_style_text_font(btn_label, &schrift_mittel_40, 0);
    lv_obj_center(btn_label);

    lv_screen_load(screen);
    lvgl_port_unlock();

    while (!s_diagnose_bestaetigt)
        vTaskDelay(pdMS_TO_TICKS(100));

    lvgl_port_lock(0);
    lv_screen_load(vorher);
    lv_obj_delete(screen);
    lvgl_port_unlock();
}

void app_main(void)
{
    esp_reset_reason_t reset_grund = esp_reset_reason();
    /* MAC gleich mit ins Log - es haengen zwei baugleiche Boards (Dev/COM3,
     * Eltern/COM5) am selben PC, ein Log allein sagt sonst nicht, welches
     * gemeint ist. WLAN-Station-MAC, dieselbe wie in "wifi:mode : sta (...)"
     * weiter unten - hier aber schon VOR jeder anderen Zeile sichtbar. */
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "Start: Seniorenuhr startet (letzter Neustart-Grund: %s, MAC %02x:%02x:%02x:%02x:%02x:%02x)",
             reset_grund_text(reset_grund), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    /* Ganz zuerst - initialisiert bei Bedarf selbst das NVS. */
    einstellungen_laden();

    zeit_zeitzone_setzen();

    ESP_ERROR_CHECK(anzeige_start());
    ESP_LOGI(TAG, "Start: Display bereit");

    /* Blackbox: war der letzte Neustart ein echter Absturz, hier - noch vor
     * dem normalen Startablauf und bevor NTP den Heartbeat-Zeitstempel
     * ueberschreibt - eine Diagnose-Meldung zeigen und auf Bestaetigung
     * warten (siehe diagnose_screen_zeigen_und_warten). */
    if (reset_ist_absturz(reset_grund)) {
        einstellungen_absturz_registrieren();
        ESP_LOGW(TAG, "Start: Neustart nach Absturz (%s) - zeige Diagnose-Meldung",
                 reset_grund_kurz(reset_grund));
        diagnose_screen_zeigen_und_warten(reset_grund);
    }
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

    /* Bewusst SCHON HIER, vor den Boot-Phasen: das Einstellungen-Menue des
     * Startbildschirms (siehe phase_verarbeiten) ist der Weg, ueber den Peter
     * am Geraet seiner Eltern nach Updates sieht - und es laeuft mitten in
     * diesen Phasen. Stand der OTA-Task erst am Ende von app_main, gab es zu
     * diesem Zeitpunkt niemanden, der die dort angestossene Pruefung
     * ausfuehrt: das Menue zeigte dann dauerhaft "Suche nach Updates...".
     * Moeglich ist der fruehe Start erst, seit der Task seinen Stack
     * statisch bekommt und damit nicht mehr auf freien Heap warten muss
     * (siehe ota.c). Er selbst tut hier noch nichts Netzlastiges - seine
     * erste Pruefung wartet die Anlaufzeit ab. */
    ota_starten();

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

    /* Wartet blockierend auf das lange Druecken des Update-Symbols. Sein
     * Stack ist statisch (siehe einstellungen_task), das Erzeugen kann also
     * nicht an fehlendem Speicher scheitern - genau daran ging es zuvor
     * zweimal. Der OTA-Task laeuft bereits seit vor den Boot-Phasen. */
    einstellungen_task_starten();

    /* app_main MUSS hier zurueckkehren: erst dadurch gibt der Idle-Task die
     * 16 KB internen SRAM des Haupt-Task-Stacks frei, und nur so bekommt der
     * gerade angestossene OTA-Task seine 8 KB (siehe ota.c). Das
     * Einstellungen-Menue laeuft deshalb in einem eigenen Task auf Zuruf
     * (siehe einstellungen_task) statt in einer Schleife an dieser Stelle. */
}
