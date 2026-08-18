/*
 * esp_log.h — Stub fuer die Host-Tests. Die getesteten Module loggen ueber
 * ESP_LOGx; auf dem PC sollen diese Zeilen die Testausgabe nicht zumuellen.
 *
 * Bewusst NICHT als leeres Makro: dann gaelten nur zum Loggen benutzte
 * Variablen als unbenutzt (-Wunused-but-set-variable) und Format-Fehler
 * fielen hier nie auf. Das "if (0)" laesst den Compiler Argumente und
 * Format-String vollstaendig pruefen, erzeugt aber keinen Code.
 */
#ifndef HOST_STUB_ESP_LOG_H
#define HOST_STUB_ESP_LOG_H

#include <stdio.h>

#define HOST_LOG_PRUEFEN(...)            \
    do {                                 \
        if (0)                           \
            printf(__VA_ARGS__);         \
    } while (0)

#define ESP_LOGE(tag, ...) do { (void)(tag); HOST_LOG_PRUEFEN(__VA_ARGS__); } while (0)
#define ESP_LOGW(tag, ...) do { (void)(tag); HOST_LOG_PRUEFEN(__VA_ARGS__); } while (0)
#define ESP_LOGI(tag, ...) do { (void)(tag); HOST_LOG_PRUEFEN(__VA_ARGS__); } while (0)
#define ESP_LOGD(tag, ...) do { (void)(tag); HOST_LOG_PRUEFEN(__VA_ARGS__); } while (0)
#define ESP_LOGV(tag, ...) do { (void)(tag); HOST_LOG_PRUEFEN(__VA_ARGS__); } while (0)

#endif
