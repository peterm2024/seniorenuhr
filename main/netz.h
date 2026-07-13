/*
 * netz.h — WLAN-Verbindung (Station-Modus) mit automatischem Reconnect.
 */
#ifndef NETZ_H
#define NETZ_H

#include <stdbool.h>
#include "esp_err.h"

/*
 * Initialisiert WLAN und versucht zu verbinden. Wartet bis zu
 * `timeout_ms` auf die erste IP-Adresse; danach kehrt die Funktion
 * zurueck, auch wenn (noch) keine Verbindung steht — im Hintergrund
 * wird die Verbindung bei Abbruch automatisch neu aufgebaut.
 *
 * Rueckgabe: ESP_OK bei Verbindung innerhalb des Timeouts,
 *            ESP_ERR_TIMEOUT sonst.
 */
esp_err_t netz_start(uint32_t timeout_ms);

/* true, sobald das Board aktuell eine IP-Adresse hat. */
bool netz_ist_verbunden(void);

#endif
