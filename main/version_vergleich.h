/*
 * version_vergleich.h — Vergleich zweier Firmware-Versionsangaben.
 *
 * Eigene Uebersetzungseinheit statt einer static-Funktion in ota.c: die Logik
 * ist reine Textauswertung, laesst sich damit auf dem PC testen
 * (test_host/test_version.c) und muss nicht ueber echte Downloads geprueft
 * werden.
 *
 * Format der Versionsangaben ist das von "git describe", das ESP-IDF in den
 * Bildkopf schreibt:
 *
 *   v0.9.3                    - genau auf dem Release-Tag
 *   v0.9.3-9-g393edfb         - 9 Commits nach dem Tag
 *   v0.9.3-9-g393edfb-dirty   - dito, mit ungesicherten Aenderungen
 *
 * Der Anlass (Peters Beobachtung, 18.08.2026): Ein selbst geflashter
 * Entwicklungsstand ist NEUER als das letzte Release, unterscheidet sich aber
 * im Text davon - die bisherige Pruefung "Text ungleich, also gibt es ein
 * Update" bot deshalb ein Downgrade auf die aeltere Version als Update an.
 */
#ifndef VERSION_VERGLEICH_H
#define VERSION_VERGLEICH_H

#include <stdbool.h>

/* true, wenn `kandidat` eine spaetere Version bezeichnet als `referenz`.
 *
 * Verglichen wird der Reihe nach Major, Minor, Patch und zuletzt die Zahl der
 * Commits seit dem Tag - ein Entwicklungsstand gilt damit als neuer als das
 * Release, auf dem er aufbaut.
 *
 * Laesst sich eine der beiden Angaben nicht deuten, wird bewusst `true`
 * geliefert, sobald sie sich ueberhaupt unterscheiden: lieber ein Update
 * anbieten, das keines ist (der Benutzer bestaetigt ohnehin von Hand), als
 * ein echtes stillschweigend zu verschlucken. */
bool version_ist_neuer(const char *kandidat, const char *referenz);

#endif
