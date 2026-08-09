/*
 * webkonfig.h — Lokale Web-Konfigurationsseite: erlaubt das Aendern der
 * Kalender-Adresse per Browser von einem beliebigen Geraet im selben WLAN
 * aus, statt der langen, kryptischen Google-Calendar-URL auf dem
 * Touchscreen (siehe einrichtung.h) abzutippen. Kein Passwort/Login -
 * dasselbe Vertrauensniveau wie das Heim-WLAN selbst.
 */
#ifndef WEBKONFIG_H
#define WEBKONFIG_H

#include <stdbool.h>

/* NUR AUF ZURUF (seit 09.08.2026, siehe FALLSTRICKE #39): der Webserver kostet
 * ca. 12 KB internen SRAM, mDNS weitere ~5 KB - zusammen genug, um jede
 * Netzoperation (Kalender, OTA) lahmzulegen, wenn der Server dauerhaft laeuft.
 * Aufgerufen jetzt nur noch aus dem Einstellungen-Menue heraus (einrichtung.c),
 * per Knopf ein-/ausgeschaltet, und beim Verlassen des Menues sicherheitshalber
 * gestoppt - die Eltern bekommen diesen Knopf nie zu Gesicht. */
void webkonfig_start(void);

/* Ein zweiter Aufruf (Server schon aus) ist ein gefahrloses No-Op. */
void webkonfig_stop(void);

bool webkonfig_laeuft(void);

#endif
