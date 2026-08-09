#include "webkonfig.h"
#include "einstellungen.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "mdns.h"

static const char *TAG = "webkonfig";
static httpd_handle_t s_server = NULL;

/* Einfache Dekodierung fuer application/x-www-form-urlencoded: "+" wird zu
 * Leerzeichen, "%XX" zum jeweiligen Byte. Reicht fuer eine einzelne
 * URL-Zeile ohne Mehrzeilen-/Multipart-Formate, ein voller Parser ist
 * dafuer nicht noetig. Wandelt in-place (Ergebnis ist nie laenger als die
 * Eingabe). */
static void url_dekodieren(char *s)
{
    char *lese = s, *schreibe = s;
    while (*lese) {
        if (*lese == '+') {
            *schreibe++ = ' ';
            lese++;
        } else if (*lese == '%' && isxdigit((unsigned char)lese[1]) && isxdigit((unsigned char)lese[2])) {
            char hex[3] = { lese[1], lese[2], '\0' };
            *schreibe++ = (char)strtol(hex, NULL, 16);
            lese += 3;
        } else {
            *schreibe++ = *lese++;
        }
    }
    *schreibe = '\0';
}

/* Escaped die Zeichen, die innerhalb eines HTML-Attributwerts (value="...")
 * gefaehrlich waeren - eine gespeicherte URL landet unveraendert wieder in
 * genau so einem Attribut (siehe start_get_handler). Ohne dies koennte eine
 * URL mit '"' das Attribut vorzeitig beenden bzw. mit '<'/'&' die Seite
 * verunstalten. ziel_groesse inklusive Nullterminierung. */
static void html_attribut_escapen(const char *quelle, char *ziel, size_t ziel_groesse)
{
    size_t pos = 0;
    for (const char *p = quelle; *p && pos + 6 < ziel_groesse; p++) {
        const char *ersatz = NULL;
        switch (*p) {
        case '&':  ersatz = "&amp;"; break;
        case '"':  ersatz = "&quot;"; break;
        case '<':  ersatz = "&lt;"; break;
        case '>':  ersatz = "&gt;"; break;
        default: break;
        }
        if (ersatz) {
            size_t n = strlen(ersatz);
            memcpy(ziel + pos, ersatz, n);
            pos += n;
        } else {
            ziel[pos++] = *p;
        }
    }
    ziel[pos] = '\0';
}

#define SEITE_KOPF \
    "<!DOCTYPE html><html lang=\"de\"><head><meta charset=\"utf-8\">" \
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">" \
    "<title>Seniorenuhr</title><style>" \
    "body{font-family:sans-serif;max-width:480px;margin:2em auto;padding:0 1em}" \
    "input{width:100%%;box-sizing:border-box;padding:.6em;font-size:1em;margin:.3em 0 1em}" \
    "button{padding:.6em 1.4em;font-size:1em}" \
    "</style></head><body>"

static esp_err_t start_get_handler(httpd_req_t *req)
{
    char url[EINSTELLUNGEN_KALENDER_URL_MAX];
    einstellungen_kalender_url_effektiv(url, sizeof url);

    char url_escaped[EINSTELLUNGEN_KALENDER_URL_MAX * 6];
    html_attribut_escapen(url, url_escaped, sizeof url_escaped);

    char seite[sizeof url_escaped + 1024];
    snprintf(seite, sizeof seite,
        SEITE_KOPF
        "<h1>Seniorenuhr</h1>"
        "<form method=\"POST\" action=\"/speichern\">"
        "<label for=\"url\">Kalender-Adresse (privater iCal-Link):</label>"
        "<input type=\"text\" id=\"url\" name=\"url\" value=\"%s\">"
        "<button type=\"submit\">Speichern</button>"
        "</form>"
        "<p>Leer lassen und speichern setzt die Standard-Adresse zurueck.</p>"
        "</body></html>",
        url_escaped);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req, seite);
    return ESP_OK;
}

static esp_err_t speichern_post_handler(httpd_req_t *req)
{
    char formular[EINSTELLUNGEN_KALENDER_URL_MAX + 16] = "";
    size_t max_lesen = req->content_len < sizeof formular - 1 ? req->content_len : sizeof formular - 1;
    int erhalten = httpd_req_recv(req, formular, max_lesen);
    if (erhalten < 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    formular[erhalten] = '\0';

    char wert[EINSTELLUNGEN_KALENDER_URL_MAX] = "";
    httpd_query_key_value(formular, "url", wert, sizeof wert);
    url_dekodieren(wert);

    einstellungen_kalender_url_setzen(wert);
    ESP_LOGI(TAG, "Kalender-Adresse per Weboberflaeche geaendert");

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

void webkonfig_start(void)
{
    if (s_server != NULL)
        return; /* schon gestartet */

    esp_err_t mdns_err = mdns_init();
    if (mdns_err == ESP_OK) {
        mdns_hostname_set("seniorenuhr");
        mdns_instance_name_set("Seniorenuhr");
    } else {
        ESP_LOGW(TAG, "mDNS konnte nicht gestartet werden: %s (Web-Konfiguration bleibt per IP erreichbar)",
                 esp_err_to_name(mdns_err));
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    /* Der GET-Handler fuer "/" legt lokale Puffer fuer URL + HTML-escapte
     * Kopie + komplette Seite an (zusammen ~4,3 KB, siehe start_get_handler)
     * - das allein fuellt den httpd-Standard-Stack von 4096 Byte schon fast
     * komplett, PLUS httpd's eigenen Verbrauch fuers Parsen der Anfrage
     * obendrauf. Live beobachtet: Panic "stack overflow in task" beim
     * ersten GET-Aufruf (gleiche Fehlerklasse wie der WLAN-Scan-Stack-
     * Overflow in netz.c, siehe FALLSTRICKE #10). Grosszuegig verdoppelt
     * statt nur knapp erhoeht. */
    config.stack_size = 8192;
    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGW(TAG, "Web-Konfigurationsserver konnte nicht gestartet werden");
        s_server = NULL;
        mdns_free();
        return;
    }

    httpd_uri_t start_uri = { .uri = "/", .method = HTTP_GET, .handler = start_get_handler };
    httpd_register_uri_handler(s_server, &start_uri);

    httpd_uri_t speichern_uri = { .uri = "/speichern", .method = HTTP_POST, .handler = speichern_post_handler };
    httpd_register_uri_handler(s_server, &speichern_uri);

    ESP_LOGI(TAG, "Web-Konfiguration erreichbar unter http://seniorenuhr.local/ (oder per IP-Adresse)");
}

void webkonfig_stop(void)
{
    if (s_server == NULL)
        return;
    httpd_stop(s_server);
    s_server = NULL;
    /* Giltiger Aufruf auch wenn mdns_init() oben fehlgeschlagen war -
     * mdns_free() ist dagegen abgesichert (No-Op ohne vorherigen init). */
    mdns_free();
    ESP_LOGI(TAG, "Web-Konfiguration ausgeschaltet");
}

bool webkonfig_laeuft(void)
{
    return s_server != NULL;
}
