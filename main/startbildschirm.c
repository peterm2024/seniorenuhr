#include "startbildschirm.h"

#include <string.h>

#include "esp_lvgl_port.h"

#define FARBE_SPLASH_GRAU 0x555555
#define FARBE_RING_AKTIV  0xaaaaaa /* heller als das Grau der Symbole, damit
                                    * der Countdown klar erkennbar ist */
#define MAX_TEILE 4

typedef struct {
    lv_obj_t *teile[MAX_TEILE];
    int anzahl;
} icon_t;

static lv_obj_t *s_screen;
static icon_t s_icons[3];
static lv_obj_t *s_ringe[3];
static int s_aktiver_schritt = -1;
static bool s_blink_an = false;
static lv_timer_t *s_blink_timer;
static lv_timer_t *s_countdown_timer;
static int s_rest_sekunden;

static void icon_teil_hinzufuegen(icon_t *ic, lv_obj_t *obj)
{
    if (ic->anzahl < MAX_TEILE)
        ic->teile[ic->anzahl++] = obj;
}

static void icon_farbe_setzen(icon_t *ic, lv_color_t farbe)
{
    for (int i = 0; i < ic->anzahl; i++) {
        lv_obj_set_style_border_color(ic->teile[i], farbe, 0);
        lv_obj_set_style_bg_color(ic->teile[i], farbe, 0);
        lv_obj_set_style_line_color(ic->teile[i], farbe, 0);
    }
}

static lv_obj_t *icon_container_erzeugen(lv_obj_t *scr, int32_t x_mitte)
{
    lv_obj_t *cont = lv_obj_create(scr);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, 100, 100);
    lv_obj_set_pos(cont, x_mitte - 50, 190);
    return cont;
}

/* Countdown-Ring um ein Symbol: voller Kreis = volle Restzeit, pro
 * Sekunde verschwindet ein Sechzigstel (im Uhrzeigersinn ab 12 Uhr).
 * Anfangs unsichtbar - erscheint erst, wenn der Schritt beginnt. */
static lv_obj_t *ring_erzeugen(lv_obj_t *scr, int32_t x_mitte)
{
    lv_obj_t *ring = lv_arc_create(scr);
    lv_obj_set_size(ring, 130, 130);
    lv_obj_set_pos(ring, x_mitte - 65, 175);
    lv_arc_set_rotation(ring, 270); /* Start oben statt rechts */
    lv_arc_set_bg_angles(ring, 0, 360);
    lv_arc_set_range(ring, 0, STARTBILDSCHIRM_PHASE_TIMEOUT_S);
    lv_arc_set_value(ring, STARTBILDSCHIRM_PHASE_TIMEOUT_S);
    lv_arc_set_mode(ring, LV_ARC_MODE_REVERSE); /* leert sich im Uhrzeigersinn */
    lv_obj_remove_style(ring, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(ring, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(ring, LV_OPA_TRANSP, LV_PART_MAIN); /* keine Hintergrund-Spur */
    lv_obj_set_style_arc_color(ring, lv_color_hex(FARBE_RING_AKTIV), LV_PART_INDICATOR);
    lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN);
    return ring;
}

static void countdown_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (s_aktiver_schritt < 0 || s_rest_sekunden <= 0)
        return;
    s_rest_sekunden--;
    lv_arc_set_value(s_ringe[s_aktiver_schritt], s_rest_sekunden);
}

/* WLAN-Symbol: vier Balken steigender Hoehe (Signalstaerke) */
static void icon_wlan_erzeugen(icon_t *ic, lv_obj_t *cont)
{
    static const int hoehen[4] = {18, 30, 42, 54};
    const int breite = 12, luecke = 8;
    const int gesamt_breite = 4 * breite + 3 * luecke;
    const int x = (100 - gesamt_breite) / 2;

    for (int i = 0; i < 4; i++) {
        lv_obj_t *balken = lv_obj_create(cont);
        lv_obj_remove_style_all(balken);
        lv_obj_set_size(balken, breite, hoehen[i]);
        lv_obj_set_style_bg_color(balken, lv_color_hex(FARBE_SPLASH_GRAU), 0);
        lv_obj_set_style_bg_opa(balken, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(balken, 3, 0);
        lv_obj_set_pos(balken, x + i * (breite + luecke), 70 - hoehen[i]);
        icon_teil_hinzufuegen(ic, balken);
    }
}

/* Uhr-Symbol: Kreis-Umrandung + zwei Zeiger */
static void icon_uhr_erzeugen(icon_t *ic, lv_obj_t *cont)
{
    lv_obj_t *kreis = lv_obj_create(cont);
    lv_obj_remove_style_all(kreis);
    lv_obj_set_size(kreis, 70, 70);
    lv_obj_set_style_radius(kreis, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(kreis, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(kreis, 6, 0);
    lv_obj_set_style_border_color(kreis, lv_color_hex(FARBE_SPLASH_GRAU), 0);
    lv_obj_center(kreis);
    icon_teil_hinzufuegen(ic, kreis);

    /* Punkte direkt in Container-Koordinaten (Kreismitte = 50,50, da der
     * 70x70-Kreis per lv_obj_center in einem 100x100-Container sitzt) -
     * die Linienobjekte selbst NICHT zusaetzlich zentrieren, sonst wird
     * jede Linie anhand ihrer eigenen (sehr schmalen) Bounding-Box neu
     * zentriert und der gemeinsame Drehpunkt geht verloren. */
    static const lv_point_precise_t minutenzeiger[2] = {{50, 50}, {50, 20}};
    lv_obj_t *minute = lv_line_create(cont);
    lv_line_set_points(minute, minutenzeiger, 2);
    lv_obj_set_style_line_width(minute, 4, 0);
    lv_obj_set_style_line_color(minute, lv_color_hex(FARBE_SPLASH_GRAU), 0);
    lv_obj_set_style_line_rounded(minute, true, 0);
    icon_teil_hinzufuegen(ic, minute);

    static const lv_point_precise_t stundenzeiger[2] = {{50, 50}, {68, 50}};
    lv_obj_t *stunde = lv_line_create(cont);
    lv_line_set_points(stunde, stundenzeiger, 2);
    lv_obj_set_style_line_width(stunde, 4, 0);
    lv_obj_set_style_line_color(stunde, lv_color_hex(FARBE_SPLASH_GRAU), 0);
    lv_obj_set_style_line_rounded(stunde, true, 0);
    icon_teil_hinzufuegen(ic, stunde);
}

/* Kalender-Symbol: abgerundetes Rechteck + Kopfleiste */
static void icon_kalender_erzeugen(icon_t *ic, lv_obj_t *cont)
{
    const int x = 15, y = 20, breite = 70, hoehe = 60, kopf_hoehe = 16;

    lv_obj_t *rahmen = lv_obj_create(cont);
    lv_obj_remove_style_all(rahmen);
    lv_obj_set_size(rahmen, breite, hoehe);
    lv_obj_set_pos(rahmen, x, y);
    lv_obj_set_style_radius(rahmen, 8, 0);
    lv_obj_set_style_bg_opa(rahmen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rahmen, 5, 0);
    lv_obj_set_style_border_color(rahmen, lv_color_hex(FARBE_SPLASH_GRAU), 0);
    icon_teil_hinzufuegen(ic, rahmen);

    lv_obj_t *kopf = lv_obj_create(cont);
    lv_obj_remove_style_all(kopf);
    lv_obj_set_size(kopf, breite, kopf_hoehe);
    lv_obj_set_pos(kopf, x, y);
    lv_obj_set_style_radius(kopf, 8, 0);
    lv_obj_set_style_bg_color(kopf, lv_color_hex(FARBE_SPLASH_GRAU), 0);
    lv_obj_set_style_bg_opa(kopf, LV_OPA_COVER, 0);
    icon_teil_hinzufuegen(ic, kopf);
}

static void blink_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (s_aktiver_schritt < 0)
        return;
    s_blink_an = !s_blink_an;
    icon_farbe_setzen(&s_icons[s_aktiver_schritt],
                      s_blink_an ? lv_color_white() : lv_color_hex(FARBE_SPLASH_GRAU));
}

void startbildschirm_erstellen(void)
{
    lvgl_port_lock(0);

    s_screen = lv_screen_active();
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);

    memset(&s_icons[STARTBILDSCHIRM_WLAN], 0, sizeof s_icons[0]);
    icon_wlan_erzeugen(&s_icons[STARTBILDSCHIRM_WLAN], icon_container_erzeugen(s_screen, 200));
    s_ringe[STARTBILDSCHIRM_WLAN] = ring_erzeugen(s_screen, 200);

    memset(&s_icons[STARTBILDSCHIRM_UHR], 0, sizeof s_icons[0]);
    icon_uhr_erzeugen(&s_icons[STARTBILDSCHIRM_UHR], icon_container_erzeugen(s_screen, 400));
    s_ringe[STARTBILDSCHIRM_UHR] = ring_erzeugen(s_screen, 400);

    memset(&s_icons[STARTBILDSCHIRM_KALENDER], 0, sizeof s_icons[0]);
    icon_kalender_erzeugen(&s_icons[STARTBILDSCHIRM_KALENDER], icon_container_erzeugen(s_screen, 600));
    s_ringe[STARTBILDSCHIRM_KALENDER] = ring_erzeugen(s_screen, 600);

    lvgl_port_unlock();
}

void startbildschirm_schritt_start(startbildschirm_schritt_t schritt)
{
    lvgl_port_lock(0);
    s_aktiver_schritt = schritt;
    s_blink_an = false;
    s_rest_sekunden = STARTBILDSCHIRM_PHASE_TIMEOUT_S;
    lv_arc_set_value(s_ringe[schritt], s_rest_sekunden);
    lv_obj_remove_flag(s_ringe[schritt], LV_OBJ_FLAG_HIDDEN);
    if (!s_blink_timer)
        s_blink_timer = lv_timer_create(blink_timer_cb, 400, NULL);
    if (!s_countdown_timer)
        s_countdown_timer = lv_timer_create(countdown_timer_cb, 1000, NULL);
    lvgl_port_unlock();
}

void startbildschirm_schritt_fertig(startbildschirm_schritt_t schritt)
{
    lvgl_port_lock(0);
    icon_farbe_setzen(&s_icons[schritt], lv_color_white());
    /* Ring wieder auffuellen und weiss stehen lassen - "geschafft" */
    lv_arc_set_value(s_ringe[schritt], STARTBILDSCHIRM_PHASE_TIMEOUT_S);
    lv_obj_set_style_arc_color(s_ringe[schritt], lv_color_white(), LV_PART_INDICATOR);
    if (s_aktiver_schritt == schritt)
        s_aktiver_schritt = -1;
    lvgl_port_unlock();
}

void startbildschirm_aufraeumen(void)
{
    lvgl_port_lock(0);
    if (s_blink_timer) {
        lv_timer_delete(s_blink_timer);
        s_blink_timer = NULL;
    }
    if (s_countdown_timer) {
        lv_timer_delete(s_countdown_timer);
        s_countdown_timer = NULL;
    }
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }
    lvgl_port_unlock();
}
