/*
 * screenshot_debug.h — Entwicklungswerkzeug: ein kleiner "Screenshot"-Button
 * auf lv_layer_top() (liegt automatisch ueber JEDEM Bildschirm, unabhaengig
 * davon welcher gerade aktiv ist) nimmt ein Bildschirmfoto des aktuell
 * aktiven LVGL-Screens auf, ausgegeben als Base64-BMP ueber die serielle
 * Verbindung (dieselbe USB-Verbindung, die beim Entwickeln fuers Flashen/
 * Monitoring ohnehin offen ist) - fuer Dokumentationszwecke. Kein Laufzeit-/
 * Netzwerk-Feature, daher keine WLAN-Abhaengigkeit. Vor dem Einzug bei
 * den Eltern wieder entfernen (siehe app_main.c).
 */
#ifndef SCREENSHOT_DEBUG_H
#define SCREENSHOT_DEBUG_H

/* Legt den Screenshot-Button auf lv_layer_top() an. Einmalig aus
 * app_main() aufzurufen, nachdem anzeige_start() das Display eingerichtet
 * hat. */
void screenshot_debug_start(void);

#endif
