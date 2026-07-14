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
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "kalender_anzeige.h"
#include "netz.h"
#include "startbildschirm.h"
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

LV_FONT_DECLARE(schrift_uhr_128);
LV_FONT_DECLARE(schrift_gross_72);
LV_FONT_DECLARE(schrift_mittel_40);
LV_FONT_DECLARE(schrift_klein_28);

static const char *TAG = "seniorenuhr";

typedef enum { MODUS_TAG, MODUS_ABEND, MODUS_NACHT } anzeige_modus_t;

static lv_obj_t *s_bildschirm;
static lv_obj_t *s_wochentag_label;
static lv_obj_t *s_uhr_label;
static lv_obj_t *s_status_label;
static lv_obj_t *s_tabletten_ueberschrift;
static lv_obj_t *s_tabletten_label;
static lv_obj_t *s_termine_ueberschrift;
static lv_obj_t *s_termine_label;
static lv_obj_t *s_dimm_overlay; /* nur fuer den Abend-Modus verwendet */

/* Bei Beruehrung waehrend Abend/Nacht wechselt die Anzeige fuer diese
 * Wachzeit vollstaendig in den Tag-Modus (siehe beruehrung_callback). */
static volatile int64_t s_wach_bis_us = 0;

static void beruehrung_callback(lv_event_t *e)
{
    (void)e;
    s_wach_bis_us = esp_timer_get_time() + BERUEHRUNG_WACHZEIT_US;
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
 * Ohne bekannte Uhrzeit gibt es nichts abzudunkeln -> Tag-Modus. */
static anzeige_modus_t aktueller_modus(void)
{
    if (!(netz_ist_verbunden() && zeit_ist_synchron()))
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

static lv_obj_t *listen_label_erzeugen(lv_obj_t *scr, int32_t x, int32_t y, int32_t breite)
{
    lv_obj_t *label = lv_label_create(scr);
    lv_obj_set_style_text_font(label, &schrift_klein_28, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(FARBE_TEXT_HELL), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, breite);
    lv_label_set_text(label, "...");
    return label;
}

static void ui_aufbauen(void)
{
    lvgl_port_lock(0);

    /* Eigener, noch nicht angezeigter Screen - waehrend die Hauptanzeige
     * hier im Hintergrund aufgebaut wird, zeigt der aktuell aktive Screen
     * den Startbildschirm (siehe app_main). */
    s_bildschirm = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_bildschirm, lv_color_hex(FARBE_TAG_HINTERGRUND), 0);
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

    /* Untere Haelfte: links Tabletten, rechts Termine - nachts ausgeblendet */
    s_tabletten_ueberschrift = ueberschrift_erzeugen(s_bildschirm, "TABLETTEN HEUTE", 20, 335, 370);
    s_tabletten_label = listen_label_erzeugen(s_bildschirm, 20, 385, 370);

    s_termine_ueberschrift = ueberschrift_erzeugen(s_bildschirm, "TERMINE HEUTE", 410, 335, 370);
    s_termine_label = listen_label_erzeugen(s_bildschirm, 410, 385, 370);

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
    bool tabletten_termine_sichtbar = (modus != MODUS_NACHT);

    lv_obj_set_style_bg_color(s_bildschirm, lv_color_hex(hintergrund), 0);
    lv_obj_set_style_text_color(s_wochentag_label, lv_color_hex(textfarbe_akzent), 0);
    lv_obj_set_style_text_color(s_uhr_label, lv_color_hex(textfarbe_hell), 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(textfarbe_status), 0);
    lv_obj_set_style_bg_opa(s_dimm_overlay, overlay_deckkraft, 0);

    if (tabletten_termine_sichtbar) {
        lv_obj_remove_flag(s_tabletten_ueberschrift, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_tabletten_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_termine_ueberschrift, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_termine_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_tabletten_ueberschrift, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_tabletten_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_termine_ueberschrift, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_termine_label, LV_OBJ_FLAG_HIDDEN);
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

    if (netz_ist_verbunden() && zeit_ist_synchron()) {
        time_t jetzt = time(NULL);
        struct tm lokal;
        localtime_r(&jetzt, &lokal);

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
     * fuehrte sonst zu sichtbarem Flackern. */
    static anzeige_modus_t letzter_modus = MODUS_TAG;
    if (modus != letzter_modus) {
        modus_anwenden(modus);
        letzter_modus = modus;
    }

    /* Termine/Tabletten nur bei tatsaechlicher Aenderung neu zeichnen,
     * nicht bei jedem Sekunden-Tick. */
    static uint32_t letzte_version = 0;
    uint32_t version = kalender_anzeige_version();
    if (version == letzte_version)
        return;
    letzte_version = version;

    kalender_anzeige_t stand;
    kalender_anzeige_kopieren(&stand);

    lvgl_port_lock(0);
    lv_label_set_text(s_tabletten_label, stand.hat_daten ? stand.tabletten_text : "...");
    lv_label_set_text(s_termine_label, stand.hat_daten ? stand.termine_text : "...");
    lvgl_port_unlock();
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
        default:
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return PHASE_TIMEOUT;
}

/* Wartet auf eine Boot-Phase und kuemmert sich um die beiden
 * Eingriffsmoeglichkeiten: "WLAN wechseln" oeffnet die Zugangsdaten-
 * Eingabe (bei "Speichern" startet das Geraet neu, bei "Abbrechen" wird
 * dieselbe Phase erneut abgewartet); "Offline" oeffnet die Datum/Uhrzeit-
 * Eingabe und markiert die AKTUELL laufende Phase danach als erledigt -
 * das gilt auch fuer die WLAN-Phase selbst, denn ab dann laeuft das
 * Geraet bewusst ohne Netz weiter (Kalenderdaten kommen dann hoechstens
 * aus dem Cache, siehe kalender_anzeige.c). */
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

        if (ergebnis == PHASE_WLAN_WECHSELN) {
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
            (void)status; /* nur ABGEBROCHEN erreichbar - Speichern startet neu */
            continue;     /* Phase erneut abwarten */
        }

        /* PHASE_OFFLINE */
        einrichtung_zeit_zeigen();
        einrichtung_status_t status;
        while ((status = einrichtung_zeit_status()) == EINRICHTUNG_OFFEN)
            vTaskDelay(pdMS_TO_TICKS(100));
        startbildschirm_reaktivieren();
        einrichtung_zeit_aufraeumen();

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
    ESP_LOGI(TAG, "Start: Seniorenuhr startet");

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
