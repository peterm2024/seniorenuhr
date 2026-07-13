/*
 * app_main.c — Seniorenuhr, Einstieg.
 *
 * Aktueller Stand (Phase 0/1): Display-Bring-up mit Test-Oberflaeche.
 * Uhrzeit ist noch ein Platzhalter; WLAN + NTP folgen als Naechstes.
 */
#include "anzeige.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

LV_FONT_DECLARE(schrift_uhr_128);
LV_FONT_DECLARE(schrift_gross_72);
LV_FONT_DECLARE(schrift_mittel_40);

static const char *TAG = "seniorenuhr";

static void test_ui_aufbauen(void)
{
    lvgl_port_lock(0);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0a1a2f), 0); /* dunkles Blau */

    lv_obj_t *wochentag = lv_label_create(scr);
    lv_label_set_text(wochentag, "MONTAG");
    lv_obj_set_style_text_font(wochentag, &schrift_gross_72, 0);
    lv_obj_set_style_text_color(wochentag, lv_color_hex(0xffd75f), 0);
    lv_obj_align(wochentag, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *uhr = lv_label_create(scr);
    lv_label_set_text(uhr, "20:45");
    lv_obj_set_style_text_font(uhr, &schrift_uhr_128, 0);
    lv_obj_set_style_text_color(uhr, lv_color_white(), 0);
    lv_obj_align(uhr, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t *gruss = lv_label_create(scr);
    lv_label_set_text(gruss, "Es funktioniert! Ä Ö Ü ä ö ü ß");
    lv_obj_set_style_text_font(gruss, &schrift_mittel_40, 0);
    lv_obj_set_style_text_color(gruss, lv_color_hex(0xd0e0f0), 0);
    lv_obj_align(gruss, LV_ALIGN_BOTTOM_MID, 0, -40);

    lvgl_port_unlock();
}

void app_main(void)
{
    ESP_LOGI(TAG, "Seniorenuhr startet");
    ESP_ERROR_CHECK(anzeige_start());
    test_ui_aufbauen();
    ESP_LOGI(TAG, "Test-Oberflaeche steht");
}
