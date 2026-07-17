#include "netz.h"
#include "secrets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NVS_NAMENSRAUM "wifi_cfg"

/* Bis zu so viele bekannte Netze werden gemerkt (z. B. das Testnetz zu
 * Hause UND das Netz bei den Eltern) - beim Voll-Werden wird das
 * aelteste (zuerst gespeicherte) verdraengt. */
#define WLAN_PROFIL_MAX 5

typedef struct {
    char ssid[33];
    char passwort[65];
} wlan_profil_t;

static const char *TAG = "netz";

/* Reisst die WLAN-Verbindung im Laufbetrieb ab und kommt laenger als diese
 * Zeit nicht wieder, hilft kein Reconnect-Versuch mehr weiter - dann lieber
 * neu starten, statt dauerhaft (z. B. mit haengenden Verbindungen)
 * steckenzubleiben. Der Watchdog ist erst NACH der ersten erfolgreichen
 * Verbindung scharf - waehrend des Bootens ueberwacht stattdessen der
 * 60s-Countdown des Startbildschirms die WLAN-Phase (app_main).
 *
 * Die scharfe 30s-Grenze gilt aber nur bis zum ERSTEN Erreichen des
 * Hauptbildschirms (siehe netz_watchdog_lockern, von app_main() aufgerufen,
 * sobald WLAN/Zeit/Kalender einmal durchgelaufen sind). Danach hat die
 * Anzeige von Uhrzeit/Tabletten/Terminen oberste Prioritaet - ein
 * Neustart-Reflex bei schwachem WLAN (siehe FALLSTRICKE #14) koennte sonst
 * eine nie endende Boot-Schleife ausloesen, waehrend der die Anzeige
 * DAUERHAFT schwarz bliebe und faellig werdende Tabletten gar nicht erst
 * angezeigt wuerden - eine zeitweise falsche Uhrzeit ist das eindeutig
 * kleinere Uebel. Ab dann greift eine viel groessere Grenze: ein Neustart
 * ist dann nur noch ein letzter Reparaturversuch, falls das WLAN wirklich
 * eine ganze Woche am Stueck nicht wiederkommt. */
#define WATCHDOG_GRENZE_KURZ_US (30LL * 1000000)
#define WATCHDOG_GRENZE_LANG_US (7LL * 24 * 3600 * 1000000LL)
#define WATCHDOG_PRUEF_INTERVALL_US (5LL * 1000000)

static volatile bool s_verbunden = false;
static volatile bool s_war_verbunden = false; /* schon je eine IP bekommen? */
static volatile bool s_watchdog_pausiert = false;
static volatile int64_t s_watchdog_grenze_us = WATCHDOG_GRENZE_KURZ_US;

/* 0 = aktuell verbunden (oder noch nie verbunden gewesen); sonst Zeitpunkt
 * (esp_timer_get_time), seit dem ununterbrochen keine Verbindung besteht. */
static volatile int64_t s_getrennt_seit_us = 0;

static void wifi_watchdog_callback(void *arg)
{
    (void)arg;
    if (s_watchdog_pausiert)
        return;
    int64_t getrennt_seit = s_getrennt_seit_us;
    if (getrennt_seit == 0)
        return;
    if (esp_timer_get_time() - getrennt_seit > s_watchdog_grenze_us) {
        ESP_LOGE(TAG, "Seit laengerer Zeit ohne WLAN-Verbindung - Neustart");
        esp_restart();
    }
}

/* Wird von app_main() aufgerufen, sobald der Hauptbildschirm zum ersten Mal
 * erreicht ist (WLAN, Zeit und Kalender einmal erfolgreich durchgelaufen) -
 * lockert die Neustart-Schwelle von 30s auf 1 Woche (siehe Kommentar oben). */
void netz_watchdog_lockern(void)
{
    s_watchdog_grenze_us = WATCHDOG_GRENZE_LANG_US;
    ESP_LOGI(TAG, "WLAN-Watchdog gelockert: Neustart erst nach 1 Woche ohne Verbindung");
}

/* Pausiert/entpausiert den Watchdog - waehrend der Benutzer auf einem
 * Einrichtungsbildschirm (WLAN-Zugangsdaten/Uhrzeit) unterwegs ist, darf
 * ein Verbindungsabbruch im Hintergrund nicht mitten in die Eingabe hinein
 * einen Neustart ausloesen (siehe app_main.c/phase_verarbeiten). Beim
 * Fortsetzen faengt die 30s-Frist neu an, falls weiterhin keine Verbindung
 * besteht - sonst wuerde die waehrend der Pause aufgelaufene Zeit sofort
 * zum Neustart fuehren. */
void netz_watchdog_pausieren(bool pausieren)
{
    s_watchdog_pausiert = pausieren;
    if (!pausieren && !s_verbunden && s_war_verbunden)
        s_getrennt_seit_us = esp_timer_get_time();
}

static void ereignis_handler(void *arg, esp_event_base_t basis, int32_t id, void *daten)
{
    (void)arg; (void)daten;

    if (basis == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_verbunden = false;
        if (s_war_verbunden && s_getrennt_seit_us == 0)
            s_getrennt_seit_us = esp_timer_get_time();
        ESP_LOGW(TAG, "WLAN-Verbindung verloren, versuche erneut...");
        esp_wifi_connect();
    } else if (basis == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_verbunden = true;
        s_war_verbunden = true;
        s_getrennt_seit_us = 0;
        ESP_LOGI(TAG, "WLAN verbunden, IP-Adresse erhalten");
    }
}

/* Leichte Verschleierung (kein kryptographisch starkes Verfahren!) mit
 * einem geraeteindividuellen Schluessel aus der Chip-MAC-Adresse - schuetzt
 * davor, dass die Passwoerter bei einem rohen Blick in einen Flash-Dump
 * direkt im Klartext lesbar sind. XOR ist selbstinvers, dieselbe Funktion
 * verschleiert und entschleiert also gleichermassen. */
static void verschleiern(uint8_t *daten, size_t laenge)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    for (size_t i = 0; i < laenge; i++)
        daten[i] ^= mac[i % sizeof mac] ^ (uint8_t)(i * 0x2B + 0x7F);
}

/* Liest die Liste bekannter Netze aus dem NVS (leer, falls noch keine
 * gespeichert wurden). Rueckgabe: Anzahl gueltiger Eintraege. */
static int profil_liste_lesen(wlan_profil_t *liste, int max_anzahl)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMENSRAUM, NVS_READONLY, &h) != ESP_OK)
        return 0;

    uint8_t anzahl_u8 = 0;
    esp_err_t err_anzahl = nvs_get_u8(h, "anzahl", &anzahl_u8);
    if (err_anzahl != ESP_OK) {
        nvs_close(h);
        return 0;
    }
    int anzahl = anzahl_u8 > max_anzahl ? max_anzahl : anzahl_u8;

    size_t blob_laenge = (size_t)anzahl * sizeof(wlan_profil_t);
    esp_err_t err = nvs_get_blob(h, "profile", liste, &blob_laenge);
    nvs_close(h);
    if (err != ESP_OK)
        return 0;

    verschleiern((uint8_t *)liste, blob_laenge);
    return anzahl;
}

static esp_err_t profil_liste_schreiben(const wlan_profil_t *liste, int anzahl)
{
    wlan_profil_t kopie[WLAN_PROFIL_MAX];
    memcpy(kopie, liste, (size_t)anzahl * sizeof(wlan_profil_t));
    verschleiern((uint8_t *)kopie, (size_t)anzahl * sizeof(wlan_profil_t));

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMENSRAUM, NVS_READWRITE, &h);
    if (err != ESP_OK)
        return err;

    err = nvs_set_u8(h, "anzahl", (uint8_t)anzahl);
    if (err == ESP_OK)
        err = nvs_set_blob(h, "profile", kopie, (size_t)anzahl * sizeof(wlan_profil_t));
    if (err == ESP_OK)
        err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t netz_zugangsdaten_speichern(const char *ssid, const char *passwort)
{
    wlan_profil_t liste[WLAN_PROFIL_MAX];
    int anzahl = profil_liste_lesen(liste, WLAN_PROFIL_MAX);

    int index_vorhanden = -1;
    for (int i = 0; i < anzahl; i++) {
        if (strcmp(liste[i].ssid, ssid) == 0) {
            index_vorhanden = i;
            break;
        }
    }

    if (index_vorhanden >= 0) {
        snprintf(liste[index_vorhanden].passwort, sizeof liste[index_vorhanden].passwort, "%s", passwort);
    } else {
        if (anzahl >= WLAN_PROFIL_MAX) {
            /* Aeltestes (zuerst gespeichertes) Profil verdraengen */
            memmove(&liste[0], &liste[1], (size_t)(WLAN_PROFIL_MAX - 1) * sizeof(wlan_profil_t));
            anzahl = WLAN_PROFIL_MAX - 1;
        }
        snprintf(liste[anzahl].ssid, sizeof liste[anzahl].ssid, "%s", ssid);
        snprintf(liste[anzahl].passwort, sizeof liste[anzahl].passwort, "%s", passwort);
        anzahl++;
    }

    esp_err_t err = profil_liste_schreiben(liste, anzahl);
    if (err != ESP_OK)
        return err;

    ESP_LOGI(TAG, "WLAN-Zugangsdaten gespeichert (%d bekannte Netze) - starte neu", anzahl);
    esp_restart();
    return ESP_OK; /* unerreichbar */
}

/* Sucht per WLAN-Scan unter den sichtbaren Netzen nach einem bekannten
 * (gespeicherten) Netz - so muss nicht blind das zuletzt gespeicherte
 * Netz probiert werden, wenn das Geraet z. B. zwischen Zuhause (Testen)
 * und den Eltern hin- und herwandert. Neuere Profile werden bei mehreren
 * Treffern bevorzugt. Ohne Treffer (oder ganz ohne gespeicherte Profile)
 * faellt die Funktion auf das zuletzt gespeicherte bzw. auf secrets.h
 * zurueck. */
static void beste_konfiguration_ermitteln(wifi_config_t *cfg, const char **quelle_aus)
{
    memset(cfg, 0, sizeof *cfg);

    wlan_profil_t profile[WLAN_PROFIL_MAX];
    int anzahl = profil_liste_lesen(profile, WLAN_PROFIL_MAX);

    uint16_t gefunden = 0;
    esp_err_t scan_err = esp_wifi_scan_start(NULL, true); /* blockierend */
    if (scan_err != ESP_OK)
        ESP_LOGW(TAG, "WLAN-Scan fehlgeschlagen: %s", esp_err_to_name(scan_err));
    esp_wifi_scan_get_ap_num(&gefunden);

    wifi_ap_record_t *aps = NULL;
    if (gefunden > 0) {
        aps = calloc(gefunden, sizeof(wifi_ap_record_t));
        if (aps)
            esp_wifi_scan_get_ap_records(&gefunden, aps);
    }

    if (aps) {
        for (int p = anzahl - 1; p >= 0; p--) {
            for (int a = 0; a < gefunden; a++) {
                if (strcmp((char *)aps[a].ssid, profile[p].ssid) == 0) {
                    snprintf((char *)cfg->sta.ssid, sizeof cfg->sta.ssid, "%s", profile[p].ssid);
                    snprintf((char *)cfg->sta.password, sizeof cfg->sta.password, "%s", profile[p].passwort);
                    *quelle_aus = "bekanntes Netz (im Scan gefunden)";
                    free(aps);
                    return;
                }
            }
        }

        /* Keines der gespeicherten Profile sichtbar - bevor blind geraten
         * wird, pruefen ob wenigstens das feste Basisnetz aus secrets.h
         * (z. B. Zuhause) im Scan auftaucht. Ohne diesen Zwischenschritt
         * wuerde sonst dauerhaft ein NICHT sichtbares Profil probiert,
         * sobald ueberhaupt irgendein Profil gespeichert ist - z. B. wenn
         * nur das Eltern-WLAN gemerkt wurde und das Geraet dann wieder
         * zuhause landet (siehe Fallstricke). */
        for (int a = 0; a < gefunden; a++) {
            if (strcmp((char *)aps[a].ssid, WLAN_SSID) == 0) {
                snprintf((char *)cfg->sta.ssid, sizeof cfg->sta.ssid, "%s", WLAN_SSID);
                snprintf((char *)cfg->sta.password, sizeof cfg->sta.password, "%s", WLAN_PASSWORT);
                *quelle_aus = "secrets.h (im Scan gefunden)";
                free(aps);
                return;
            }
        }
        free(aps);
    }

    if (anzahl > 0) {
        /* Nichts Bekanntes im Scan sichtbar (oder Scan fehlgeschlagen) -
         * trotzdem das zuletzt gespeicherte Profil probieren, falls der
         * Scan z. B. unvollstaendig war. */
        snprintf((char *)cfg->sta.ssid, sizeof cfg->sta.ssid, "%s", profile[anzahl - 1].ssid);
        snprintf((char *)cfg->sta.password, sizeof cfg->sta.password, "%s", profile[anzahl - 1].passwort);
        *quelle_aus = "zuletzt gespeichertes Netz (kein bekanntes Netz im Scan sichtbar)";
        return;
    }

    snprintf((char *)cfg->sta.ssid, sizeof cfg->sta.ssid, "%s", WLAN_SSID);
    snprintf((char *)cfg->sta.password, sizeof cfg->sta.password, "%s", WLAN_PASSWORT);
    *quelle_aus = "secrets.h";
}

void netz_start(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    const esp_timer_create_args_t watchdog_cfg = {
        .callback = wifi_watchdog_callback,
        .name = "wifi_watchdog",
    };
    esp_timer_handle_t watchdog;
    ESP_ERROR_CHECK(esp_timer_create(&watchdog_cfg, &watchdog));
    ESP_ERROR_CHECK(esp_timer_start_periodic(watchdog, WATCHDOG_PRUEF_INTERVALL_US));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &ereignis_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ereignis_handler, NULL));

    /* STA-Modus schon vor der Konfigurationswahl starten - der Scan in
     * beste_konfiguration_ermitteln() braucht dafuer keine Verbindung,
     * nur den gestarteten WLAN-Treiber. Erst danach wird die ausgewaehlte
     * SSID/Passwort gesetzt und explizit verbunden (kein Auto-Connect
     * mehr auf WIFI_EVENT_STA_START, das wuerde die Scan-Auswahl umgehen). */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_config_t wifi_cfg;
    const char *quelle;
    beste_konfiguration_ermitteln(&wifi_cfg, &quelle);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.sta.pmf_cfg.capable = true;
    wifi_cfg.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));

    /* WLAN-Stromsparmodus aus: Das Board haengt an einem Netzteil, Strom
     * sparen ist unnoetig - der periodische Modem-Schlaf/Aufwach-Zyklus
     * konkurriert sonst mit dem RGB-Display um die PSRAM-Bandbreite und
     * zeigt sich als gelegentliches Flackern, das mit keiner erkennbaren
     * Aktion im Programm zusammenhaengt. */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_LOGI(TAG, "Verbinde mit WLAN (Zugangsdaten: %s)...", quelle);
    ESP_ERROR_CHECK(esp_wifi_connect());
}

bool netz_ist_verbunden(void)
{
    return s_verbunden;
}

int netz_rssi_dbm(void)
{
    wifi_ap_record_t info;
    if (esp_wifi_sta_get_ap_info(&info) != ESP_OK)
        return 0;
    return info.rssi;
}
