/*
 * esp_err.h — Stub fuer die Host-Tests. Nur die im getesteten Code
 * tatsaechlich verwendeten Rueckgabewerte, mit denselben Zahlenwerten wie
 * ESP-IDF, damit ein Test nicht versehentlich etwas anderes prueft als das
 * Geraet spaeter liefert.
 */
#ifndef HOST_STUB_ESP_ERR_H
#define HOST_STUB_ESP_ERR_H

typedef int esp_err_t;

#define ESP_OK                0
#define ESP_FAIL             -1
#define ESP_ERR_INVALID_ARG   0x102

#endif
