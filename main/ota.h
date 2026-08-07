/*
 * ota.h — Firmware-Updates ueber GitHub, ohne dass jemand vor Ort sein muss
 * (siehe FAHRPLAN.md Phase 5). Ein Hintergrund-Task prueft periodisch
 * https://github.com/peterm2024/seniorenuhr/releases/latest/download/seniorenuhr.bin
 * und spielt eine neuere Version automatisch ein - Peters Ablauf dafuer ist
 * nur noch "git tag vX.Y.Z && git push --tags", der Rest passiert von
 * selbst (siehe .github/workflows/release.yml).
 *
 * Rollback-Schutz (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE, siehe
 * sdkconfig.defaults): kommt eine frisch eingespielte Firmware nicht sauber
 * hoch, faellt der Bootloader beim naechsten Start von selbst auf die
 * vorherige Version zurueck - ota_starten() bestaetigt eine gerade erst
 * eingespielte Version deshalb erst, sobald das Geraet nachweislich
 * brauchbar ist (WLAN verbunden, Kalender einmal erfolgreich geladen).
 */
#ifndef OTA_H
#define OTA_H

#include <stdbool.h>

/* True, waehrend gerade eine gefundene Aktualisierung heruntergeladen wird -
 * app_main.c blendet dann ein Hinweisfenster ein (siehe
 * tagesansicht_update_fenster_zeigen) und unterdrueckt bis zum Abschluss
 * das Tabletten-Erinnerungsfenster, damit sich beide Overlays nicht
 * ueberlappen. */
bool ota_laeuft(void);

/* Fortschritt in Prozent (0..100), nur waehrend ota_laeuft() aussagekraeftig.
 * Liefert -1, wenn der Server keine Content-Length meldet (dann bleibt der
 * genaue Fortschritt unbekannt - kommt bei GitHub-Release-Assets praktisch
 * nicht vor, aber sicherheitshalber abgefangen). */
int ota_fortschritt_prozent(void);

/* Startet den Hintergrund-Task (eigener Task, 8192 Byte Stack - der
 * HTTPS-Handshake fuer den Firmware-Download braucht wie beim
 * Kalender-Download etwas mehr Stack als der Rest der Anwendung, siehe
 * kalender_anzeige.c). Einmalig aus app_main() aufzurufen, nachdem
 * netz_start() lief. */
void ota_starten(void);

#endif
