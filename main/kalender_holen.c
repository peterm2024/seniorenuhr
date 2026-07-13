#include "kalender_holen.h"
#include "secrets.h"

#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "kalender_holen";

#define ANFANGSGROESSE (16 * 1024)
#define MAX_BYTES (512 * 1024)

typedef struct {
    char *puffer;
    size_t belegt;
    size_t kapazitaet;
} wachsender_puffer_t;

static esp_err_t kapazitaet_sicherstellen(wachsender_puffer_t *p, size_t zusaetzlich)
{
    if (p->belegt + zusaetzlich <= p->kapazitaet)
        return ESP_OK;

    size_t neu_kapazitaet = p->kapazitaet ? p->kapazitaet * 2 : ANFANGSGROESSE;
    while (neu_kapazitaet < p->belegt + zusaetzlich)
        neu_kapazitaet *= 2;
    if (neu_kapazitaet > MAX_BYTES)
        return ESP_ERR_NO_MEM;

    char *neu = heap_caps_realloc(p->puffer, neu_kapazitaet, MALLOC_CAP_SPIRAM);
    if (!neu)
        return ESP_ERR_NO_MEM;

    p->puffer = neu;
    p->kapazitaet = neu_kapazitaet;
    return ESP_OK;
}

static esp_err_t ereignis_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA)
        return ESP_OK;

    wachsender_puffer_t *p = (wachsender_puffer_t *)evt->user_data;
    if (kapazitaet_sicherstellen(p, (size_t)evt->data_len) != ESP_OK) {
        ESP_LOGE(TAG, "Kalender zu gross (> %d Bytes) - abgebrochen", MAX_BYTES);
        return ESP_FAIL;
    }
    memcpy(p->puffer + p->belegt, evt->data, evt->data_len);
    p->belegt += evt->data_len;
    return ESP_OK;
}

esp_err_t kalender_holen(char **puffer, size_t *laenge)
{
    wachsender_puffer_t p = {0};
    if (kapazitaet_sicherstellen(&p, ANFANGSGROESSE) != ESP_OK)
        return ESP_ERR_NO_MEM;

    esp_http_client_config_t cfg = {
        .url = KALENDER_ICS_URL,
        .event_handler = ereignis_handler,
        .user_data = &p,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "Download fehlgeschlagen (err=%s, HTTP-Status=%d)",
                 esp_err_to_name(err), status);
        free(p.puffer);
        return (err == ESP_OK) ? ESP_FAIL : err;
    }

    *puffer = p.puffer;
    *laenge = p.belegt;
    return ESP_OK;
}
