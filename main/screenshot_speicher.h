/*
 * screenshot_speicher.h — Ringpuffer fuer Bildschirmfotos in einer eigenen
 * Flash-Partition ("screenshots", siehe partitions.csv), analog zum
 * Core-Dump (sdkconfig.defaults, FALLSTRICKE #40): entsteht ein Screenshot,
 * waehrend kein serieller Monitor mitlaeuft (z.B. Board am Router, per
 * Powerbank betrieben), ist die serielle Ausgabe wirkungslos - hier landet
 * dieselbe Aufnahme zusaetzlich dauerhaft im Flash und laesst sich beim
 * naechsten Anstecken abholen (tools/screenshot_flash_abholen.py). Peters
 * Idee, Anlass war ein Router-Test ohne jede Moeglichkeit zum Mitschnitt.
 *
 * Bewusst NICHT auf ENTWICKLUNGSWERKZEUGE beschraenkt: seit das Screenshot-
 * Werkzeug auch im Produktions-Build per Einstellungen-Menue einschaltbar
 * ist (siehe screenshot_debug.h), gilt fuer die Flash-Ablage dieselbe
 * Erwaegung - genau der Elternmodus-Router-Test war der Anlass fuer diese
 * Partition. Kostet sonst brachliegenden Flash, keinen internen SRAM.
 */
#ifndef SCREENSHOT_SPEICHER_H
#define SCREENSHOT_SPEICHER_H

#include <stdbool.h>
#include <stdint.h>

/* Legt "daten" (BMP-Kopf + Pixeldaten, roh oder RLE-komprimiert - exakt
 * dasselbe Format, das auch seriell verschickt wird, siehe
 * screenshot_debug.c) im naechsten freien Ringpuffer-Platz ab. Ueberschreibt
 * nach einer vollen Runde den jeweils aeltesten Platz. Passt "daten" nicht
 * hinein, wird NICHT gespeichert (nur eine Log-Warnung) - lieber eine
 * fehlende als eine abgeschnittene Aufnahme. */
void screenshot_speicher_ablegen(const uint8_t *daten, uint32_t groesse,
                                  uint16_t breite, uint16_t hoehe, bool komprimiert);

#endif
