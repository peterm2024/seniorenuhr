/*
 * webkonfig.h — Lokale Web-Konfigurationsseite: erlaubt das Aendern der
 * Kalender-Adresse per Browser von einem beliebigen Geraet im selben WLAN
 * aus, statt der langen, kryptischen Google-Calendar-URL auf dem
 * Touchscreen (siehe einrichtung.h) abzutippen. Kein Passwort/Login -
 * dasselbe Vertrauensniveau wie das Heim-WLAN selbst.
 */
#ifndef WEBKONFIG_H
#define WEBKONFIG_H

/* Startet den Webserver samt mDNS-Namen "seniorenuhr.local" - aufzurufen,
 * sobald zum ersten Mal eine WLAN-Verbindung besteht (siehe netz.c). Ein
 * zweiter Aufruf (z. B. nach einem Reconnect) ist ein gefahrloses No-Op. */
void webkonfig_start(void);

#endif
