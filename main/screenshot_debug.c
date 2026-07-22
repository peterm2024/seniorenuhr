#include "screenshot_debug.h"

#include <string.h>

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"

/* PRIVATE LVGL-Header - bewusst fuer diesen reinen Entwicklungs-Zweck
 * genutzt (siehe Kommentar bei screenshot_vollstaendig_rendern unten).
 * Nicht Teil der offiziellen API, koennte sich mit einer neuen LVGL-Version
 * aendern - unkritisch, da dieses ganze Modul vor dem Einzug bei Peters
 * Eltern wieder aus app_main.c entfernt wird. */
#include "core/lv_refr_private.h"
#include "display/lv_display_private.h"

#define BMP_HEADER_GROESSE 54

/* Referenzmarke zur Versatz-Selbstkontrolle (Peters Idee): eine schmale
 * vertikale Linie in einer im UI sonst nie verwendeten Signalfarbe wird NUR
 * fuer die Aufnahme kurz als Kind des Screens hinzugefuegt - sie durchlaeuft
 * damit denselben Render-Weg wie alle anderen Objekte. Findet das
 * Dekodier-Skript (tools/screenshot_dekodieren.py) sie nicht an ihrer
 * Soll-Spalte wieder, hat ein Versatz stattgefunden (Render ODER serielle
 * Uebertragung), und der gemessene Abstand verraet ihn exakt; das Skript
 * rechnet die Marke danach wieder heraus. Da sie unter dem LVGL-Lock
 * hinzugefuegt und noch vor dem Freigeben wieder geloescht wird, sieht der
 * echte Display-Refresh sie nie (kein Aufblitzen auf dem Geraet). */
#define REFERENZ_MARKE_X 10
#define REFERENZ_MARKE_BREITE 2
#define REFERENZ_MARKE_FARBE 0xFF00FF /* Magenta - im UI (Blau/Grau/Weiss/Gelb/Schwarz) nirgends verwendet */

static const char *TAG = "screenshot_debug";
static lv_obj_t *s_button;
static lv_obj_t *s_label;
/* Verhindert eine zweite ueberlappende Aufnahme: der Button wird schon
 * waehrend der (langsamen) Uebertragung wieder eingeblendet ("Sende...",
 * siehe screenshot_task) und waere damit erneut antippbar. */
static volatile bool s_laeuft = false;

/* Ersetzt lv_snapshot_take_to_draw_buf() durch ein eigenes, IMMER
 * vollstaendiges Neuzeichnen (entspricht dessen "top_obj == obj"-Zweig,
 * siehe lv_snapshot.c). Grund: lv_snapshot_take_to_draw_buf() versucht per
 * lv_refr_get_top_obj() eine Teil-Optimierung ("nur das Noetigste neu
 * zeichnen") - die geht davon aus, dass der Zielpuffer bereits den
 * vorherigen Frame enthaelt (wie beim echten Bildschirm-Refresh) und
 * ueberspringt deshalb Geschwister-Objekte VOR dem gefundenen "top object".
 * Bei einem frischen, leeren Snapshot-Puffer fuehrte das live dazu, dass
 * einige Wochentag-Buttons an einer voellig falschen Position (verschoben
 * zum rechten statt am linken Bildschirmrand) landeten. Nutzt bewusst zwei
 * private LVGL-Funktionen (siehe Includes oben) - fuer dieses reine
 * Entwicklungswerkzeug vertretbar. */
static void screenshot_vollstaendig_rendern(lv_obj_t *screen, lv_draw_buf_t *draw_buf)
{
    lv_layer_t layer;
    lv_layer_init(&layer);

    /* Den Puffer an der tatsaechlichen linken oberen Ecke des Screens
     * verankern (NICHT hart auf {0,0}), genauso wie es lv_snapshot.c macht.
     * Sitzt der Screen exakt bei (0,0) - der Normalfall im Ruhezustand -
     * ist das identisch zu {0,0,w-1,h-1}. Steht der Screen aber
     * voruebergehend verschoben (live beobachtet: waehrend des Bootens war
     * der Startbildschirm um 121px versetzt, vermutlich ein transienter
     * Layout-/Transitionszustand), zeichnete die alte {0,0}-Variante ALLE
     * Objekte um genau diesen Versatz daneben - auf dem echten Display
     * unsichtbar (dessen Refresh rechnet korrekt), nur im Screenshot. Durch
     * Ausrichten von buf_area/clip an den Screen-Koordinaten landen die
     * Kinder immer relativ zum Screen-Ursprung korrekt im Bild. */
    lv_area_t screen_bereich;
    lv_obj_get_coords(screen, &screen_bereich);
    /* Die ext_draw_size (ueberstehende Schatten/Umrisse) lassen wir bewusst
     * weg - ein Screen hat keine, und die noetige Funktion ist ausserhalb
     * von LVGL nicht oeffentlich deklariert. */

    layer.draw_buf = draw_buf;
    layer.buf_area.x1 = screen_bereich.x1;
    layer.buf_area.y1 = screen_bereich.y1;
    layer.buf_area.x2 = screen_bereich.x1 + (int32_t)draw_buf->header.w - 1;
    layer.buf_area.y2 = screen_bereich.y1 + (int32_t)draw_buf->header.h - 1;
    layer.color_format = LV_COLOR_FORMAT_RGB888;
    layer._clip_area = screen_bereich;
    layer.phy_clip_area = screen_bereich;

    lv_draw_unit_send_event(NULL, LV_EVENT_CHILD_CREATED, &layer);

    lv_display_t *disp_alt = lv_refr_get_disp_refreshing();
    lv_display_t *disp_neu = lv_obj_get_display(screen);
    lv_layer_t *layer_alt = disp_neu->layer_head;
    disp_neu->layer_head = &layer;
    lv_refr_set_disp_refreshing(disp_neu);

    lv_obj_redraw(&layer, screen);

    layer.all_tasks_added = true;
    while (layer.draw_task_head) {
        lv_draw_dispatch_wait_for_request();
        lv_draw_dispatch();
    }

    disp_neu->layer_head = layer_alt;
    lv_refr_set_disp_refreshing(disp_alt);

    lv_draw_unit_send_event(NULL, LV_EVENT_SCREEN_LOAD_START, &layer);
    lv_draw_unit_send_event(NULL, LV_EVENT_CHILD_DELETED, &layer);
}

/* Baut aus "screen" ein 24bpp-BMP (Kopf + Pixeldaten in einem
 * zusammenhaengenden PSRAM-Puffer, Reserve am Anfang fuer den Kopf).
 * Rueckgabe: Zeiger auf den Puffer (vom Aufrufer per heap_caps_free()
 * freizugeben) oder NULL bei Fehler; *datei_groesse_aus liefert die
 * tatsaechliche Groesse.
 *
 * Puffer wird SELBST per heap_caps_malloc(...SPIRAM) alloziert statt ueber
 * die bequemere lv_snapshot_take(): letztere holt ihren Speicher ueber
 * lv_malloc(), das hier aber auf den bewusst kleinen, festen 64-KB-LVGL-
 * Pool begrenzt ist (siehe FALLSTRICKE #16, sdkconfig.defaults
 * CONFIG_LV_MEM_SIZE_KILOBYTES=64) - ein Screenshot-Puffer fuer 800x480
 * braucht dagegen gut 1 MB. */
static uint8_t *screenshot_aufnehmen(lv_obj_t *screen, uint32_t *datei_groesse_aus)
{
    lvgl_port_lock(0);
    lv_obj_update_layout(screen);
    uint32_t w = (uint32_t)lv_obj_get_width(screen);
    uint32_t h = (uint32_t)lv_obj_get_height(screen);
    lvgl_port_unlock();

    uint32_t stride = lv_draw_buf_width_to_stride(w, LV_COLOR_FORMAT_RGB888);
    size_t puffer_groesse = (size_t)stride * (size_t)h;

    uint8_t *gesamt_puffer = heap_caps_malloc(BMP_HEADER_GROESSE + puffer_groesse, MALLOC_CAP_SPIRAM);
    if (!gesamt_puffer) {
        ESP_LOGW(TAG, "Kein PSRAM fuer Screenshot-Puffer (%u Byte)",
                 (unsigned)(BMP_HEADER_GROESSE + puffer_groesse));
        return NULL;
    }
    uint8_t *pixel_puffer = gesamt_puffer + BMP_HEADER_GROESSE;
    memset(pixel_puffer, 0, puffer_groesse); /* sauberer schwarzer Hintergrund */

    lv_draw_buf_t draw_buf = {0};
    draw_buf.header.magic = LV_IMAGE_HEADER_MAGIC;
    draw_buf.header.cf = LV_COLOR_FORMAT_RGB888;
    draw_buf.header.w = w;
    draw_buf.header.h = h;
    draw_buf.header.stride = stride;
    draw_buf.data = pixel_puffer;
    draw_buf.data_size = puffer_groesse;

    lvgl_port_lock(0);
    /* Referenzmarke temporaer als oberstes Kind hinzufuegen (siehe Kommentar
     * bei REFERENZ_MARKE_X oben) - noch VOR dem Rendern, damit sie mit im
     * Bild landet, und direkt DANACH wieder loeschen, noch unter demselben
     * Lock, damit der echte Display-Refresh sie nie zu sehen bekommt. */
    lv_obj_t *referenz_marke = lv_obj_create(screen);
    lv_obj_remove_style_all(referenz_marke);
    lv_obj_set_size(referenz_marke, REFERENZ_MARKE_BREITE, (int32_t)h);
    lv_obj_set_pos(referenz_marke, REFERENZ_MARKE_X, 0);
    lv_obj_set_style_bg_color(referenz_marke, lv_color_hex(REFERENZ_MARKE_FARBE), 0);
    lv_obj_set_style_bg_opa(referenz_marke, LV_OPA_COVER, 0);
    lv_obj_remove_flag(referenz_marke, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_update_layout(screen);

    screenshot_vollstaendig_rendern(screen, &draw_buf);

    lv_obj_delete(referenz_marke);
    lvgl_port_unlock();

    uint32_t zeilen_bytes = w * 3;
    uint32_t bild_groesse = zeilen_bytes * h;

    uint8_t *kopf = gesamt_puffer;
    memset(kopf, 0, BMP_HEADER_GROESSE);
    kopf[0] = 'B'; kopf[1] = 'M';
    uint32_t datei_groesse = BMP_HEADER_GROESSE + bild_groesse;
    memcpy(&kopf[2], &datei_groesse, 4);
    uint32_t pixel_offset = BMP_HEADER_GROESSE;
    memcpy(&kopf[10], &pixel_offset, 4);
    uint32_t dib_groesse = 40;
    memcpy(&kopf[14], &dib_groesse, 4);
    int32_t breite = (int32_t)w, hoehe_negativ = -(int32_t)h;
    memcpy(&kopf[18], &breite, 4);
    memcpy(&kopf[22], &hoehe_negativ, 4);
    uint16_t ebenen = 1, bpp = 24;
    memcpy(&kopf[26], &ebenen, 2);
    memcpy(&kopf[28], &bpp, 2);
    memcpy(&kopf[34], &bild_groesse, 4);

    *datei_groesse_aus = datei_groesse;
    return gesamt_puffer;
}

/* Base64-kodiert das BMP und gibt es zeilenweise ueber ESP_LOGI aus,
 * eingerahmt von klaren Markierungen. */
static void screenshot_base64_ausgeben(const uint8_t *bmp, uint32_t datei_groesse, uint32_t w, uint32_t h)
{
    size_t base64_groesse = 0;
    mbedtls_base64_encode(NULL, 0, &base64_groesse, bmp, datei_groesse);
    char *base64_puffer = heap_caps_malloc(base64_groesse, MALLOC_CAP_SPIRAM);
    if (!base64_puffer) {
        ESP_LOGW(TAG, "Kein PSRAM fuer Base64-Puffer (%u Byte)", (unsigned)base64_groesse);
        return;
    }
    size_t base64_laenge = 0;
    mbedtls_base64_encode((unsigned char *)base64_puffer, base64_groesse, &base64_laenge, bmp, datei_groesse);

    /* Zeilenweise ausgeben (feste Breite, wie klassisches MIME-Base64) -
     * ein einziger riesiger Log-Aufruf waere unhandlich/evtl. abgeschnitten.
     * Alle paar Zeilen kurz per vTaskDelay() an den Scheduler abgeben: ohne
     * das lief diese Schleife ueber die volle ~2 Minuten am Stueck durch,
     * ohne dem IDLE-Task Gelegenheit zu geben zu laufen - der IDLE-Task ist
     * standardmaessig ebenfalls beim Task-Watchdog angemeldet, dessen
     * Ausbleiben loeste live einen Neustart mitten in der Uebertragung aus
     * (obwohl dieser Task selbst nicht ueberwacht wird, siehe Kommentar
     * oben - das Verhungern eines ANDEREN ueberwachten Tasks reicht auch). */
    const size_t ZEILENBREITE = 120;
    ESP_LOGI(TAG, "-----BEGIN SCREENSHOT %ux%u-----", (unsigned)w, (unsigned)h);
    int zeilen_nr = 0;
    for (size_t pos = 0; pos < base64_laenge; pos += ZEILENBREITE) {
        size_t rest = base64_laenge - pos;
        size_t n = rest < ZEILENBREITE ? rest : ZEILENBREITE;
        esp_log_write(ESP_LOG_INFO, TAG, "%.*s\n", (int)n, base64_puffer + pos);
        if (++zeilen_nr % 20 == 0)
            vTaskDelay(pdMS_TO_TICKS(2));
    }
    ESP_LOGI(TAG, "-----END SCREENSHOT-----");

    heap_caps_free(base64_puffer);
}

/* Erledigt die eigentliche Arbeit in einem EIGENEN Task statt direkt im
 * Button-Klick-Callback: Letzterer laeuft auf dem von esp_lvgl_port
 * ueberwachten LVGL-Task (Task-Watchdog, siehe FALLSTRICKE #16) - das
 * serielle Ausgeben von ~1,5 MB Base64-Text (rund 12.800 einzelne
 * Log-Zeilen) dauert bei 115200 Baud gut zwei Minuten und sprengt damit
 * bei weitem das 5s-Watchdog-Fenster, live beobachtet als sofortiger
 * Neustart nach jedem Antippen. Ein per xTaskCreate() frisch erzeugter
 * Task ist NICHT beim Watchdog angemeldet und darf beliebig lange
 * brauchen, ohne die Uhr-Anzeige zu blockieren oder einen Neustart
 * auszuloesen.
 *
 * Reihenfolge bewusst so gewaehlt, dass der Button nur waehrend der
 * (schnellen) Bildaufnahme ausgeblendet bleibt - waehrend der (langsamen)
 * Uebertragung zeigt er stattdessen "Sende...", damit klar ist, dass das
 * Geraet arbeitet und nicht einfach nur den Tipp ignoriert hat (Peters
 * Wunsch nach einer Fortschrittsanzeige). */
static void screenshot_task(void *arg)
{
    lv_obj_t *screen = (lv_obj_t *)arg;

    uint32_t datei_groesse = 0;
    uint8_t *bmp = screenshot_aufnehmen(screen, &datei_groesse);

    lvgl_port_lock(0);
    if (bmp)
        lv_label_set_text(s_label, "Sende...");
    lv_obj_remove_flag(s_button, LV_OBJ_FLAG_HIDDEN);
    lvgl_port_unlock();

    if (bmp) {
        /* w/h fuer die BEGIN-Markierung aus dem BMP-Kopf zurueckgewinnen,
         * statt sie zusaetzlich durchzureichen. */
        int32_t breite, hoehe_negativ;
        memcpy(&breite, &bmp[18], 4);
        memcpy(&hoehe_negativ, &bmp[22], 4);
        screenshot_base64_ausgeben(bmp, datei_groesse, (uint32_t)breite, (uint32_t)(-hoehe_negativ));
        heap_caps_free(bmp);
    }

    lvgl_port_lock(0);
    lv_label_set_text(s_label, "Screenshot");
    lvgl_port_unlock();

    s_laeuft = false;
    vTaskDelete(NULL);
}

/* Blendet den Button waehrend der Aufnahme aus (er liegt auf lv_layer_top(),
 * ist also selbst kein Kind von lv_screen_active() und wuerde deshalb
 * vermutlich ohnehin nicht mitfotografiert - das Ausblenden ist trotzdem
 * die robustere, von Peter vorgeschlagene Variante, falls lv_snapshot
 * intern doch ueberlappende Top-Layer-Objekte beruecksichtigt). */
static void button_geklickt_cb(lv_event_t *e)
{
    (void)e;
    if (s_laeuft) {
        ESP_LOGW(TAG, "Screenshot laeuft schon - Tipp ignoriert");
        return;
    }
    s_laeuft = true;

    lvgl_port_lock(0);
    lv_obj_add_flag(s_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *screen = lv_screen_active();
    lvgl_port_unlock();

    if (xTaskCreate(screenshot_task, "screenshot_dump", 8192, (void *)screen, 4, NULL) != pdPASS) {
        ESP_LOGW(TAG, "Screenshot-Task konnte nicht gestartet werden");
        s_laeuft = false;
        lvgl_port_lock(0);
        lv_obj_remove_flag(s_button, LV_OBJ_FLAG_HIDDEN);
        lvgl_port_unlock();
    }
}

void screenshot_debug_start(void)
{
    lvgl_port_lock(0);
    /* lv_layer_top() liegt automatisch UEBER jedem per lv_screen_load()
     * aktivierten Screen, unabhaengig davon welcher gerade aktiv ist - der
     * Button muss also nur EINMAL hier angelegt werden, nicht in jeder
     * einzelnen Bildschirm-Aufbaufunktion. Der Top-Layer selbst ist nicht
     * klickbar, daher werden Antippungen ausserhalb des Buttons ganz normal
     * an den darunterliegenden echten Screen durchgereicht. */
    lv_obj_t *top = lv_layer_top();

    s_button = lv_button_create(top);
    lv_obj_set_size(s_button, 130, 40);
    lv_obj_align(s_button, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_bg_color(s_button, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_bg_opa(s_button, LV_OPA_50, 0);
    lv_obj_add_event_cb(s_button, button_geklickt_cb, LV_EVENT_CLICKED, NULL);

    s_label = lv_label_create(s_button);
    lv_label_set_text(s_label, "Screenshot");
    lv_obj_center(s_label);

    lvgl_port_unlock();
    ESP_LOGI(TAG, "Screenshot-Debug bereit: Button unten Mitte antippen fuer ein Bildschirmfoto ueber die serielle Ausgabe");
}
