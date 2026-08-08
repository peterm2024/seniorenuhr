#include "netz.h"
#include "secrets.h"
#include "webkonfig.h"

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
#include "freertos/task.h"
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

static esp_netif_t *s_sta_netif;
static volatile bool s_verbunden = false;
/* Beruht die laufende Verbindung auf den einkompilierten Zugangsdaten? Dann
 * werden sie nach dem ersten Erfolg in den NVS uebernommen, damit sie ein
 * Firmware-Update ueberleben (siehe secrets_profil_uebernehmen). */
static volatile bool s_quelle_ist_secrets = false;
/* Aufgeschobene Arbeit aus dem Ereignis-Handler heraus - siehe dort. */
static volatile bool s_wartung_faellig = false;
static volatile bool s_war_verbunden = false; /* schon je eine IP bekommen? */
static volatile bool s_watchdog_pausiert = false;
static volatile int64_t s_watchdog_grenze_us = WATCHDOG_GRENZE_KURZ_US;

/* Ergebnis des zuletzt gestarteten Einstellungen-Scans (siehe
 * netz_scan_starten) - wird aus dem Event-Handler heraus befuellt (anderer
 * Task als der lesende UI-Timer), aber erst NACH vollstaendigem Befuellen
 * wird s_scan_fertig gesetzt, daher reicht hier ein einfaches volatile
 * Flag ohne Mutex (gleiches Muster wie s_verbunden/s_war_verbunden oben). */
static netz_scan_eintrag_t s_scan_ergebnisse[NETZ_SCAN_MAX];
static volatile int s_scan_anzahl = 0;
static volatile bool s_scan_fertig = false;
/* true nur waehrend eines von netz_scan_starten() ausgeloesten Scans -
 * beste_konfiguration_ermitteln() (unten) fuehrt beim Boot ebenfalls einen
 * eigenen (blockierenden) Scan durch und liest dessen Ergebnisse selbst per
 * esp_wifi_scan_get_ap_records() aus. Ohne dieses Unterscheidungs-Flag wuerde
 * der globale WIFI_EVENT_SCAN_DONE-Handler unten AUCH auf diesen Boot-Scan
 * reagieren und ihm per eigenem esp_wifi_scan_get_ap_records()-Aufruf die
 * Ergebnisse wegschnappen (die Funktion liefert die interne Ergebnisliste
 * nur einmal aus) - das fuehrte live zu Verbindungsproblemen beim Booten. */
static volatile bool s_scan_von_uns = false;

/* 0 = aktuell verbunden (oder noch nie verbunden gewesen); sonst Zeitpunkt
 * (esp_timer_get_time), seit dem ununterbrochen keine Verbindung besteht. */
static volatile int64_t s_getrennt_seit_us = 0;

/* true, solange der WLAN-Einrichtungsbildschirm offen ist (siehe
 * netz_verbindungsversuche_pausieren): Der Reconnect-Kreislauf (jedes
 * STA_DISCONNECTED startet sofort den naechsten esp_wifi_connect) haelt das
 * Funkmodul sonst dauerhaft in einem Verbindungsversuch, und JEDER
 * esp_wifi_scan_start aus dem Einrichtungsbildschirm scheitert mit
 * ESP_ERR_WIFI_STATE - die Netzwerkliste bleibt leer. Zu Hause fiel das nie
 * auf (Heimnetz sichtbar -> verbunden -> kein Reconnect aktiv); beim
 * Aufstellen bei den Eltern (kein gespeichertes Netz sichtbar) fand die
 * Suche deshalb keine einzige SSID. */
static volatile bool s_verbindungsversuche_pausiert = false;

/* Neuverbindungs-Scan im Laufbetrieb: Der Reconnect nach einem Abbruch
 * probiert stur immer wieder das ZULETZT verbundene Netz - ein erneuter
 * Scan ueber alle gespeicherten Profile passierte bisher nur beim Boot
 * (beste_konfiguration_ermitteln). Live beobachtet: nach einem Hotspot-Test
 * ("Peters iPhone" gespeichert und verbunden, dann Hotspot aus) blieb das
 * Geraet dauerhaft offline, obwohl das Heimnetz die ganze Zeit sichtbar
 * war - erst ein Stromziehen half. Deshalb: bleibt die Verbindung im
 * Laufbetrieb laenger als NEUSCAN_NACH_US weg, laeuft die Boot-Auswahl
 * (Scan ueber alle Profile, bestes sichtbares Netz) erneut - in einer
 * eigenen kleinen Task, denn der blockierende Scan darf weder auf der
 * esp_timer- noch auf der winzigen esp_event-Task laufen (2304 Bytes,
 * siehe Kommentar im Scan-Done-Handler). Nur im gelockerten Betrieb
 * (nach dem ersten Erreichen des Hauptbildschirms) - waehrend des Bootens
 * uebernehmen der 60s-Countdown bzw. der scharfe 30s-Watchdog. */
#define NEUSCAN_NACH_US (60LL * 1000000)
static volatile bool s_neuscan_laeuft = false;
static int64_t s_naechster_neuscan_us = 0; /* nur von der esp_timer-Task benutzt */

static void beste_konfiguration_ermitteln(wifi_config_t *cfg, const char **quelle_aus);

static void neuverbindung_task(void *arg)
{
    (void)arg;
    /* Einen evtl. gerade laufenden Verbindungsversuch abbrechen - waehrend
     * des Verbindens wuerde esp_wifi_scan_start fehlschlagen. Das dabei
     * ausgeloeste STA_DISCONNECTED-Ereignis startet wegen s_neuscan_laeuft
     * keinen neuen Versuch (siehe ereignis_handler). */
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(200));

    wifi_config_t cfg;
    const char *quelle;
    beste_konfiguration_ermitteln(&cfg, &quelle);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;
    esp_wifi_set_config(WIFI_IF_STA, &cfg);

    ESP_LOGI(TAG, "Neuverbindung nach Scan mit: %s", quelle);
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK)
        ESP_LOGW(TAG, "Neuverbindung konnte nicht gestartet werden: %s (naechster Versuch "
                      "beim naechsten Neuscan)", esp_err_to_name(err));
    /* Erst NACH dem connect-Aufruf freigeben: schlaegt die Verbindung fehl,
     * kommt STA_DISCONNECTED und der normale Reconnect-Kreislauf (und damit
     * auch der naechste Neuscan in 60s) laeuft wieder. */
    s_neuscan_laeuft = false;
    vTaskDelete(NULL);
}

static void wifi_watchdog_callback(void *arg)
{
    (void)arg;
    if (s_watchdog_pausiert)
        return;
    int64_t getrennt_seit = s_getrennt_seit_us;
    if (getrennt_seit == 0)
        return;
    int64_t offline_us = esp_timer_get_time() - getrennt_seit;
    if (offline_us > s_watchdog_grenze_us) {
        ESP_LOGE(TAG, "Seit laengerer Zeit ohne WLAN-Verbindung - Neustart");
        esp_restart();
    }

    /* Laufbetrieb-Neuscan (siehe Kommentar bei NEUSCAN_NACH_US): fruehestens
     * alle 60s einen anstossen, und nie zwei gleichzeitig. */
    if (s_watchdog_grenze_us == WATCHDOG_GRENZE_LANG_US &&
        offline_us > NEUSCAN_NACH_US && !s_neuscan_laeuft &&
        !s_verbindungsversuche_pausiert &&
        esp_timer_get_time() >= s_naechster_neuscan_us) {
        s_naechster_neuscan_us = esp_timer_get_time() + NEUSCAN_NACH_US;
        s_neuscan_laeuft = true;
        ESP_LOGW(TAG, "Seit %llds ohne WLAN - suche per Scan nach dem besten bekannten Netz",
                 offline_us / 1000000);
        if (xTaskCreate(neuverbindung_task, "wifi_neuscan", 4096, NULL, 5, NULL) != pdPASS) {
            s_neuscan_laeuft = false; /* kein Speicher - naechster Versuch in 60s */
            ESP_LOGE(TAG, "Neuverbindungs-Task konnte nicht gestartet werden");
        }
    }
}

/* Wird von app_main() aufgerufen, sobald der Hauptbildschirm zum ersten Mal
 * erreicht ist (WLAN, Zeit und Kalender einmal erfolgreich durchgelaufen) -
 * lockert die Neustart-Schwelle von 30s auf 1 Woche (siehe Kommentar oben). */
void netz_watchdog_lockern(void)
{
    s_watchdog_grenze_us = WATCHDOG_GRENZE_LANG_US;
    /* Lief der Boot komplett OHNE Verbindung durch (Offline-Fallback),
     * ist s_getrennt_seit_us noch 0 (wird sonst nur nach einem Abbruch
     * einer bestehenden Verbindung gesetzt - waehrend des Bootens bewusst
     * nicht, sonst Neustart-Schleife, siehe FALLSTRICKE #14). Ab jetzt
     * soll die Offline-Zeit aber zaehlen, damit auch in diesem Fall der
     * periodische Neuverbindungs-Scan (neuverbindung_task) anspringt -
     * z. B. unterwegs gebootet und danach zurueck in Reichweite des
     * Heimnetzes gekommen. */
    if (!s_verbunden && s_getrennt_seit_us == 0)
        s_getrennt_seit_us = esp_timer_get_time();
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

/* Haelt den Reconnect-Kreislauf an, solange der WLAN-Einrichtungsbildschirm
 * offen ist - sonst blockiert der Dauer-Verbindungsversuch jeden Scan (siehe
 * Kommentar bei s_verbindungsversuche_pausiert). Beim Pausieren wird ein
 * gerade laufender Verbindungsversuch aktiv abgebrochen (gleiches Muster wie
 * neuverbindung_task), damit der erste Scan nicht erst dessen Timeout
 * abwarten muss; eine BESTEHENDE Verbindung bleibt unangetastet (verbunden
 * darf das Funkmodul ohnehin scannen). Beim Fortsetzen wird der Kreislauf
 * wieder angeworfen, falls keine Verbindung besteht. */
void netz_verbindungsversuche_pausieren(bool pausieren)
{
    s_verbindungsversuche_pausiert = pausieren;
    if (pausieren) {
        if (!s_verbunden) {
            ESP_LOGI(TAG, "Verbindungsversuche pausiert (Einrichtungsbildschirm offen)");
            esp_wifi_disconnect();
        }
    } else if (!s_verbunden) {
        ESP_LOGI(TAG, "Verbindungsversuche fortgesetzt");
        esp_wifi_connect();
    }
}

static void ereignis_handler(void *arg, esp_event_base_t basis, int32_t id, void *daten)
{
    (void)arg; (void)daten;

    if (basis == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_verbunden = false;
        if (s_war_verbunden && s_getrennt_seit_us == 0)
            s_getrennt_seit_us = esp_timer_get_time();
        /* Waehrend des Neuverbindungs-Scans (neuverbindung_task) keinen
         * neuen Verbindungsversuch starten - ein laufender Verbindungs-
         * aufbau wuerde dessen Scan zum Fehlschlagen bringen. Die Task
         * selbst ruft am Ende esp_wifi_connect() auf und haelt damit den
         * Reconnect-Kreislauf am Leben. Gleiches gilt fuer den offenen
         * WLAN-Einrichtungsbildschirm (siehe s_verbindungsversuche_pausiert)
         * - dessen Ende wirft den Kreislauf selbst wieder an. */
        if (s_neuscan_laeuft || s_verbindungsversuche_pausiert)
            return;
        ESP_LOGW(TAG, "WLAN-Verbindung verloren, versuche erneut...");
        esp_wifi_connect();
    } else if (basis == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_verbunden = true;
        s_war_verbunden = true;
        s_getrennt_seit_us = 0;
        ESP_LOGI(TAG, "WLAN verbunden, IP-Adresse erhalten");
        /* Erst jetzt ist bewiesen, dass die Zugangsdaten stimmen. Steht die
         * Verbindung ueber secrets.h, sollen sie in den NVS - sonst waere das
         * Geraet nach dem naechsten Update ohne Netz (die Release-Firmware
         * enthaelt nur Platzhalter).
         *
         * Hier wird aber NUR ein Merker gesetzt, nicht geschrieben: dieser
         * Handler laeuft im Task "sys_evt" mit gerade einmal 2304 Byte Stack
         * (CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE). Der Schreibweg legt drei
         * wlan_profil_t[5]-Arrays uebereinander an (je 490 Byte) und sprengte
         * ihn prompt - live als "stack overflow in task sys_evt" abgestuerzt.
         * Ausgefuehrt wird die Arbeit deshalb in einem Task mit
         * ordentlichem Stack, siehe netz_wartung_ausfuehren(). */
        if (s_quelle_ist_secrets) {
            s_quelle_ist_secrets = false;
            s_wartung_faellig = true;
        }
        /* Bei jeder (Wieder-)Verbindung aufgerufen - webkonfig_start() ist
         * intern gegen Mehrfachstart abgesichert, startet also nur beim
         * allerersten Erfolg wirklich. Hier statt in app_main(), damit die
         * Web-Konfiguration auch dann verfuegbar wird, wenn die erste
         * Verbindung erst per Laufbetrieb-Neuscan (s.o.) zustande kommt. */
        webkonfig_start();
    } else if (basis == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        if (!s_scan_von_uns)
            return; /* fremder Scan (z. B. beste_konfiguration_ermitteln beim Boot) - nicht anfassen */
        s_scan_von_uns = false;

        uint16_t gefunden = 0;
        esp_wifi_scan_get_ap_num(&gefunden);
        /* static statt lokal: dieser Handler laeuft auf der esp_event-
         * System-Task, die nur CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=2304
         * Bytes Stack hat. wifi_ap_record_t ist (mit Country-/HE-AP-Info)
         * ueber 80 Bytes gross - ein lokales Array von NETZ_SCAN_MAX=16
         * Eintraegen haette allein ueber 1,3 KB des winzigen Stacks belegt
         * und ihn gesprengt (live beobachtet: Bildschirm zeigte wirre
         * Symbole, danach haengte sich das Geraet komplett auf - gleiche
         * Fehlerklasse wie FALLSTRICKE_UND_WORKAROUNDS.md #10). Der Handler
         * ist nicht reentrant (WIFI_EVENT_SCAN_DONE kommt erst nach dem
         * naechsten Scan wieder), ein statischer Puffer ist daher sicher. */
        static wifi_ap_record_t aps[NETZ_SCAN_MAX];
        uint16_t tatsaechlich = gefunden < NETZ_SCAN_MAX ? gefunden : NETZ_SCAN_MAX;
        if (tatsaechlich > 0)
            esp_wifi_scan_get_ap_records(&tatsaechlich, aps);
        else
            tatsaechlich = 0;

        int anzahl = 0;
        for (int a = 0; a < tatsaechlich && anzahl < NETZ_SCAN_MAX; a++) {
            if (aps[a].ssid[0] == '\0')
                continue; /* verstecktes Netz ohne Namen - nur per Hand eingebbar */
            bool duplikat = false;
            for (int i = 0; i < anzahl; i++) {
                if (strcmp(s_scan_ergebnisse[i].ssid, (char *)aps[a].ssid) == 0) {
                    if ((int8_t)aps[a].rssi > s_scan_ergebnisse[i].rssi)
                        s_scan_ergebnisse[i].rssi = (int8_t)aps[a].rssi;
                    duplikat = true;
                    break;
                }
            }
            if (duplikat)
                continue;
            snprintf(s_scan_ergebnisse[anzahl].ssid, sizeof s_scan_ergebnisse[anzahl].ssid,
                     "%s", (char *)aps[a].ssid);
            s_scan_ergebnisse[anzahl].rssi = (int8_t)aps[a].rssi;
            anzahl++;
        }
        /* Nach Signalstaerke absteigend sortieren - Bubble-Sort reicht bei
         * maximal NETZ_SCAN_MAX Eintraegen locker aus. */
        for (int i = 0; i < anzahl - 1; i++) {
            for (int j = 0; j < anzahl - 1 - i; j++) {
                if (s_scan_ergebnisse[j].rssi < s_scan_ergebnisse[j + 1].rssi) {
                    netz_scan_eintrag_t tausch = s_scan_ergebnisse[j];
                    s_scan_ergebnisse[j] = s_scan_ergebnisse[j + 1];
                    s_scan_ergebnisse[j + 1] = tausch;
                }
            }
        }
        s_scan_anzahl = anzahl;
        s_scan_fertig = true;
        ESP_LOGI(TAG, "WLAN-Scan (Einstellungen) fertig: %d Netz(e) gefunden", anzahl);
        for (int i = 0; i < anzahl; i++)
            ESP_LOGI(TAG, "  [%d] SSID=\"%s\" RSSI=%d", i, s_scan_ergebnisse[i].ssid,
                     s_scan_ergebnisse[i].rssi);
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

/* Legt ein Profil im NVS ab, OHNE neu zu starten. Der Neustart gehoert zur
 * Benutzer-Eingabe (netz_zugangsdaten_speichern), nicht zum stillen Merken
 * einer bereits funktionierenden Verbindung. */
static esp_err_t zugangsdaten_ablegen(const char *ssid, const char *passwort, int *anzahl_aus)
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
    if (err == ESP_OK && anzahl_aus)
        *anzahl_aus = anzahl;
    return err;
}

esp_err_t netz_zugangsdaten_speichern(const char *ssid, const char *passwort)
{
    int anzahl = 0;
    esp_err_t err = zugangsdaten_ablegen(ssid, passwort, &anzahl);
    if (err != ESP_OK)
        return err;

    ESP_LOGI(TAG, "WLAN-Zugangsdaten gespeichert (%d bekannte Netze) - starte neu", anzahl);
    esp_restart();
    return ESP_OK; /* unerreichbar */
}

/* Uebernimmt die einkompilierten Zugangsdaten aus secrets.h einmalig in den
 * NVS, sobald sie sich als funktionierend erwiesen haben.
 *
 * Anlass ist ein echter Vorfall: nach dem ersten erfolgreichen OTA-Update
 * hatte das Geraet kein WLAN mehr. Der Release-Workflow ersetzt secrets.h
 * bewusst durch Platzhalter ("Netzwerkname eintragen"), damit kein echtes
 * Passwort in einer oeffentlichen Binary landet - ein Geraet, das nur an
 * secrets.h haengt, verliert damit aber mit jedem Update seinen Netzzugang.
 * Und ohne Netz kommt nie wieder ein Update an: es kaeme nur noch per Kabel
 * oder durch manuelle Eingabe am Touchscreen zurueck.
 *
 * Im NVS ueberleben die Daten jedes Update (die Partition wird von OTA nicht
 * angefasst). Damit ist das Geraet nach dem ersten erfolgreichen Verbinden
 * unabhaengig davon, was in der jeweiligen Firmware einkompiliert ist. */
static void secrets_profil_uebernehmen(void)
{
    wlan_profil_t liste[WLAN_PROFIL_MAX];
    int anzahl = profil_liste_lesen(liste, WLAN_PROFIL_MAX);
    for (int i = 0; i < anzahl; i++)
        if (strcmp(liste[i].ssid, WLAN_SSID) == 0)
            return; /* schon bekannt */

    int neu = 0;
    if (zugangsdaten_ablegen(WLAN_SSID, WLAN_PASSWORT, &neu) == ESP_OK)
        ESP_LOGI(TAG, "Zugangsdaten aus secrets.h in den NVS uebernommen (%d bekannte Netze) - "
                      "das WLAN bleibt damit auch nach einem Firmware-Update erhalten", neu);
}

bool netz_wartung_faellig(void)
{
    return s_wartung_faellig;
}

/* Darf NUR aus einem Task mit ordentlichem Stack aufgerufen werden (>= 4 KB),
 * niemals aus einem Ereignis-Handler: der Weg in den NVS haelt drei
 * wlan_profil_t[5]-Arrays gleichzeitig auf dem Stack. */
void netz_wartung_ausfuehren(void)
{
    if (!s_wartung_faellig)
        return;
    s_wartung_faellig = false;
    secrets_profil_uebernehmen();
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
    s_sta_netif = esp_netif_create_default_wifi_sta();

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

    /* Laenderkennung Deutschland: Ohne diese Angabe laeuft der Treiber im
     * "World Safe Mode" (Kennung "01") und scannt nur die Kanaele 1-11
     * vollwertig - deutsche Router duerfen aber auch auf Kanal 12/13 senden
     * und werden dann bei der Netzwerksuche uebersehen. Das Geraet steht
     * fest in Deutschland. Der zweite Parameter (true) laesst den Treiber
     * die Kennung nach dem Verbinden per 802.11d vom Router uebernehmen. */
    ESP_ERROR_CHECK(esp_wifi_set_country_code("DE", true));

    wifi_config_t wifi_cfg;
    const char *quelle;
    beste_konfiguration_ermitteln(&wifi_cfg, &quelle);
    /* Merken, ob diese Verbindung auf den einkompilierten Zugangsdaten
     * beruht - nur dann lohnt es, sie nach dem Erfolg in den NVS zu
     * uebernehmen (siehe secrets_profil_uebernehmen). */
    s_quelle_ist_secrets = (strcmp((char *)wifi_cfg.sta.ssid, WLAN_SSID) == 0);
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

void netz_ip_text(char *puffer, size_t puffer_groesse)
{
    puffer[0] = '\0';
    if (!s_verbunden || !s_sta_netif)
        return;
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(s_sta_netif, &info) == ESP_OK)
        snprintf(puffer, puffer_groesse, IPSTR, IP2STR(&info.ip));
}

void netz_ssid_text(char *puffer, size_t puffer_groesse)
{
    puffer[0] = '\0';
    wifi_ap_record_t info;
    if (esp_wifi_sta_get_ap_info(&info) == ESP_OK)
        snprintf(puffer, puffer_groesse, "%s", (const char *)info.ssid);
}

void netz_scan_starten(void)
{
    s_scan_fertig = false;
    s_scan_anzahl = 0;
    s_scan_von_uns = true;
    /* Laengere Verweildauer pro Kanal als der Standard (120ms): iPhone-
     * Hotspots beaconen im Leerlauf nur sparsam und antworten auf Probe-
     * Requests oft traege - mit dem Standardwert wurde "Peters iPhone"
     * regelmaessig uebersehen, obwohl es auf Kanal 6 mit vollem Signal
     * sichtbar war (per PC-Scan verifiziert). ~300ms x 13 Kanaele ergibt
     * rund 4s pro Scan-Runde - unkritisch, da der Scan asynchron laeuft
     * und die UI ihn nur pollt (wlan_scan_tick_cb). */
    wifi_scan_config_t cfg = {
        .scan_time = { .active = { .min = 0, .max = 300 } },
    };
    esp_err_t err = esp_wifi_scan_start(&cfg, false); /* nicht blockierend */
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WLAN-Scan (Einstellungen) konnte nicht gestartet werden: %s",
                 esp_err_to_name(err));
        s_scan_von_uns = false;
        s_scan_fertig = true; /* leeres Ergebnis - die UI soll nicht ewig warten */
    }
}

bool netz_scan_fertig(void)
{
    return s_scan_fertig;
}

int netz_scan_ergebnisse(netz_scan_eintrag_t *ziel, int max)
{
    int anzahl = s_scan_anzahl < max ? s_scan_anzahl : max;
    memcpy(ziel, s_scan_ergebnisse, sizeof ziel[0] * (size_t)anzahl);
    return anzahl;
}
