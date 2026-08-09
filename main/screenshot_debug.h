/*
 * screenshot_debug.h — Entwicklungswerkzeug: ein kleiner "Screenshot"-Button
 * auf lv_layer_top() (liegt automatisch ueber JEDEM Bildschirm, unabhaengig
 * davon welcher gerade aktiv ist) nimmt ein Bildschirmfoto des aktuell
 * aktiven LVGL-Screens auf, ausgegeben als Base64-BMP ueber die serielle
 * Verbindung (dieselbe USB-Verbindung, die beim Entwickeln fuers Flashen/
 * Monitoring ohnehin offen ist) - fuer Dokumentationszwecke. Kein Laufzeit-/
 * Netzwerk-Feature, daher keine WLAN-Abhaengigkeit.
 *
 * Im Dev-Build (ENTWICKLUNGSWERKZEUGE, siehe app_main.c) startet der Button
 * automatisch beim Boot. Seit 09.08.2026 zusaetzlich per Einstellungen-Menue
 * (einrichtung.c) EIN-/AUSSCHALTBAR - auch im Produktions-Build ("Elternmodus"),
 * fuer den seltenen Fall, dass vor Ort ein Bildschirmfoto gebraucht wird. Die
 * Eltern selbst sehen den Knopf trotzdem nie: das Einstellungen-Menue ist fuer
 * sie ohnehin unerreichbar (siehe Zugangskonzept in einrichtung.c).
 */
#ifndef SCREENSHOT_DEBUG_H
#define SCREENSHOT_DEBUG_H

#include <stdbool.h>

/* Legt den Screenshot-Button auf lv_layer_top() an. Mehrfacher Aufruf ohne
 * dazwischenliegendes screenshot_debug_stop() ist ein No-Op (kein doppelter
 * Button). */
void screenshot_debug_start(void);

/* Entfernt den Button wieder. Laeuft gerade eine Aufnahme/Uebertragung
 * (screenshot_task), wird das Abschalten verschoben statt den Button unter
 * dem laufenden Task wegzureissen - screenshot_debug_laeuft() bleibt dann
 * bewusst noch "true", bis der Task fertig ist und man erneut ausschaltet. */
void screenshot_debug_stop(void);

/* Fuer die Knopf-Beschriftung im Einstellungen-Menue (an/aus). */
bool screenshot_debug_laeuft(void);

#endif
