/*
 * anzeige.c — Display-Bring-up fuer das Waveshare ESP32-S3-Touch-LCD-7.
 *
 * Bausteine:
 *   - CH422G-IO-Expander (I2C): schaltet Touch-Reset, Backlight, LCD-Reset
 *   - esp_lcd RGB-Panel: 800x480, 16-bit-Parallelbus, Framebuffer im PSRAM
 *   - GT911-Touch (I2C)
 *   - esp_lvgl_port: verbindet alles mit LVGL (Anti-Tearing ueber
 *     zwei Framebuffer + Bounce-Buffer)
 *
 * Pin-Belegung laut Waveshare-Wiki bzw. ESPHome-Paket fuer dieses Board.
 */
#include "anzeige.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

static const char *TAG = "anzeige";

/* --- Pins ----------------------------------------------------------- */
#define PIN_I2C_SDA    8
#define PIN_I2C_SCL    9
#define PIN_TOUCH_INT  4

#define PIN_LCD_DE     5
#define PIN_LCD_HSYNC  46
#define PIN_LCD_VSYNC  3
#define PIN_LCD_PCLK   7
/* Datenleitungen D0..D15 = B3..B7, G2..G7, R3..R7 */
#define PINS_LCD_DATEN {14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40}

#define LCD_BREITE 800
#define LCD_HOEHE  480

/* --- CH422G-IO-Expander ---------------------------------------------
 * Eigenwilliger Chip: statt Registern hat er feste I2C-Adressen.
 * 0x24 = Systemkonfiguration (0x01 aktiviert Push-Pull-Ausgaenge),
 * 0x38 = Ausgangspegel der Pins EXIO0..7.
 * Belegung auf diesem Board: EXIO1 = Touch-Reset, EXIO2 = Backlight,
 * EXIO3 = LCD-Reset. Alle drei auf High = Display an. */
static esp_err_t ch422g_einschalten(i2c_master_bus_handle_t bus)
{
    i2c_master_dev_handle_t konfig, ausgang;
    i2c_device_config_t cfg = {
        .device_address = 0x24,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &konfig), TAG,
                        "CH422G (0x24) nicht erreichbar");
    cfg.device_address = 0x38;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &ausgang), TAG,
                        "CH422G (0x38) nicht erreichbar");

    uint8_t push_pull = 0x01;
    uint8_t pegel = 0x0E; /* EXIO1+2+3 High */
    ESP_RETURN_ON_ERROR(i2c_master_transmit(konfig, &push_pull, 1, 100), TAG,
                        "CH422G-Konfiguration fehlgeschlagen");
    ESP_RETURN_ON_ERROR(i2c_master_transmit(ausgang, &pegel, 1, 100), TAG,
                        "CH422G-Ausgaenge fehlgeschlagen");
    return ESP_OK;
}

/* --- RGB-Panel ------------------------------------------------------- */
static esp_err_t panel_erzeugen(esp_lcd_panel_handle_t *panel)
{
    const esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = 16 * 1000 * 1000,
            .h_res = LCD_BREITE,
            .v_res = LCD_HOEHE,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 16,
            .vsync_front_porch = 16,
            .flags.pclk_active_neg = true,
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = 2,                                  /* Anti-Tearing */
        /* Grosszuegig bemessen: der Bounce-Buffer puffert PSRAM-Zugriffs-
         * verzoegerungen ab, die u.a. durch WLAN-Aktivitaet entstehen
         * (gemeinsamer Speicherbus) - zu klein bemessen zeigt sich das
         * als gelegentliches Flackern. */
        .bounce_buffer_size_px = LCD_BREITE * 20,
        .hsync_gpio_num = PIN_LCD_HSYNC,
        .vsync_gpio_num = PIN_LCD_VSYNC,
        .de_gpio_num = PIN_LCD_DE,
        .pclk_gpio_num = PIN_LCD_PCLK,
        .disp_gpio_num = -1,
        .data_gpio_nums = PINS_LCD_DATEN,
        .flags.fb_in_psram = true,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&cfg, panel), TAG,
                        "RGB-Panel anlegen fehlgeschlagen");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*panel), TAG, "Panel-Reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*panel), TAG, "Panel-Init");
    return ESP_OK;
}

/* --- GT911-Touch ------------------------------------------------------ */
static esp_lcd_touch_handle_t touch_erzeugen(i2c_master_bus_handle_t bus)
{
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    if (esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io) != ESP_OK)
        return NULL;

    const esp_lcd_touch_config_t cfg = {
        .x_max = LCD_BREITE,
        .y_max = LCD_HOEHE,
        .rst_gpio_num = GPIO_NUM_NC,   /* Reset laeuft ueber CH422G */
        .int_gpio_num = PIN_TOUCH_INT,
    };
    esp_lcd_touch_handle_t touch = NULL;
    if (esp_lcd_touch_new_i2c_gt911(io, &cfg, &touch) != ESP_OK)
        return NULL;
    return touch;
}

/* --- Gesamt-Start ----------------------------------------------------- */
esp_err_t anzeige_start(void)
{
    /* I2C-Bus (CH422G + Touch) */
    i2c_master_bus_handle_t i2c_bus;
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &i2c_bus), TAG, "I2C-Bus");

    ESP_RETURN_ON_ERROR(ch422g_einschalten(i2c_bus), TAG, "CH422G");

    esp_lcd_panel_handle_t panel;
    ESP_RETURN_ON_ERROR(panel_erzeugen(&panel), TAG, "Panel");

    /* LVGL starten und Display anmelden */
    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "LVGL-Port");

    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = panel,
        .buffer_size = LCD_BREITE * LCD_HOEHE,
        .double_buffer = true,
        .hres = LCD_BREITE,
        .vres = LCD_HOEHE,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .direct_mode = true,
        },
    };
    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = true,
        },
    };
    lv_display_t *display = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    ESP_RETURN_ON_FALSE(display != NULL, ESP_FAIL, TAG, "Display-Anmeldung");

    /* Touch ist optional — ohne Touch laeuft die Anzeige trotzdem */
    esp_lcd_touch_handle_t touch = touch_erzeugen(i2c_bus);
    if (touch) {
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = display,
            .handle = touch,
        };
        lvgl_port_add_touch(&touch_cfg);
        ESP_LOGI(TAG, "Touch (GT911) aktiv");
    } else {
        ESP_LOGW(TAG, "Touch nicht gefunden - Anzeige laeuft ohne Touch");
    }

    ESP_LOGI(TAG, "Display laeuft (800x480)");
    return ESP_OK;
}
