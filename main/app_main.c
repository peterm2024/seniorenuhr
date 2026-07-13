/*
 * app_main.c — Seniorenuhr, Einstieg.
 *
 * Phase 1: WLAN + NTP, echte laufende Uhr mit deutschem Wochentag/Datum.
 * Solange keine Zeit bekannt ist, wird das offen angezeigt statt einer
 * falschen Uhrzeit (siehe FAHRPLAN.md).
 */
#include <time.h>

#include "anzeige.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "netz.h"
#include "zeit.h"

LV_FONT_DECLARE(schrift_uhr_128);
LV_FONT_DECLARE(schrift_gross_72);
LV_FONT_DECLARE(schrift_mittel_40);

static const char *TAG = "seniorenuhr";

static lv_obj_t *s_wochentag_label;
static lv_obj_t *s_uhr_label;
static lv_obj_t *s_status_label;

static void ui_aufbauen(void)
{
    lvgl_port_lock(0);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0a1a2f), 0);

    s_wochentag_label = lv_label_create(scr);
    lv_label_set_text(s_wochentag_label, "...");
    lv_obj_set_style_text_font(s_wochentag_label, &schrift_gross_72, 0);
    lv_obj_set_style_text_color(s_wochentag_label, lv_color_hex(0xffd75f), 0);
    lv_obj_align(s_wochentag_label, LV_ALIGN_TOP_MID, 0, 30);

    s_uhr_label = lv_label_create(scr);
    lv_label_set_text(s_uhr_label, "--:--");
    lv_obj_set_style_text_font(s_uhr_label, &schrift_uhr_128, 0);
    lv_obj_set_style_text_color(s_uhr_label, lv_color_white(), 0);
    lv_obj_align(s_uhr_label, LV_ALIGN_CENTER, 0, -10);

    s_status_label = lv_label_create(scr);
    lv_label_set_text(s_status_label, "Verbinde mit WLAN...");
    lv_obj_set_style_text_font(s_status_label, &schrift_mittel_40, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xd0e0f0), 0);
    lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_MID, 0, -40);

    lvgl_port_unlock();
}

/* Wird jede Sekunde von LVGL aufgerufen (siehe app_main). */
static void uhr_tick(lv_timer_t *timer)
{
    (void)timer;

    char uhrzeit[8];
    char datum[32];
    const char *wochentag;
    const char *status;

    if (netz_ist_verbunden() && zeit_ist_synchron()) {
        time_t jetzt = time(NULL);
        struct tm lokal;
        localtime_r(&jetzt, &lokal);

        snprintf(uhrzeit, sizeof uhrzeit, "%02d:%02d", lokal.tm_hour, lokal.tm_min);
        wochentag = zeit_wochentag_gross(&lokal);
        zeit_datum_text(&lokal, datum, sizeof datum);
        status = datum;
    } else {
        snprintf(uhrzeit, sizeof uhrzeit, "--:--");
        wochentag = "...";
        status = netz_ist_verbunden() ? "Uhrzeit wird geholt..." : "Warte auf WLAN...";
    }

    lvgl_port_lock(0);
    lv_label_set_text(s_uhr_label, uhrzeit);
    lv_label_set_text(s_wochentag_label, wochentag);
    lv_label_set_text(s_status_label, status);
    lvgl_port_unlock();
}

void app_main(void)
{
    ESP_LOGI(TAG, "Seniorenuhr startet");

    zeit_zeitzone_setzen();

    esp_err_t wlan_ergebnis = netz_start(15000);
    if (wlan_ergebnis != ESP_OK)
        ESP_LOGW(TAG, "WLAN noch nicht verbunden, versuche im Hintergrund weiter");

    zeit_sntp_starten();

    ESP_ERROR_CHECK(anzeige_start());
    ui_aufbauen();

    lv_timer_create(uhr_tick, 1000, NULL);

    ESP_LOGI(TAG, "Uhr laeuft");
}
