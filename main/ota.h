/*
 * ota.h — Firmware-Updates ueber GitHub, ohne dass jemand vor Ort sein muss
 * (siehe FAHRPLAN.md Phase 5). Ein Hintergrund-Task prueft periodisch
 * https://github.com/peterm2024/seniorenuhr/releases/latest/download/seniorenuhr.bin
 * Peters Ablauf zum Veroeffentlichen ist nur "git tag vX.Y.Z && git push
 * --tags" (siehe .github/workflows/release.yml).
 *
 * INSTALLIERT wird jedoch nichts von selbst (Peters Entscheidung): Findet
 * die Pruefung eine neue Version, erscheint lediglich ein Symbol auf dem
 * Hauptbildschirm. Eingespielt wird erst auf ausdruecklichen Tipp im
 * Einstellungen-Menue. So landet nie unangekuendigt eine neue Oberflaeche
 * bei seinen Eltern - und wenn eine Version nicht gefaellt, laesst sich
 * ueber dasselbe Menue auf die vorherige zurueckschalten.
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
#include <stddef.h>

#include "esp_err.h"

/* Laenge einer Versionsangabe ("v1.2.3") und wie viele Versionen die
 * Auswahlliste hoechstens aufnimmt. Bewusst knapp: die Liste soll auf einen
 * Bildschirm passen und nicht zum Archiv werden. */
#define OTA_VERSION_MAX    24
#define OTA_VERSIONEN_MAX  10

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

/* Klartext-Meldung fuers Fortschrittsfenster, solange etwas zu sagen ist
 * ("Verbinde mit GitHub...", "Keine Verbindung - bitte spaeter erneut
 * versuchen"). Leerer String = nichts zu melden, dann gilt der Prozentwert.
 * Grund fuer diesen Zusatz: schlaegt schon der Verbindungsaufbau fehl, gibt
 * es keinen Fortschritt zu zeigen - ohne Meldung wirkte der Update-Knopf
 * schlicht wirkungslos. */
const char *ota_meldung(void);

/* Startet den Hintergrund-Task (eigener Task, 8192 Byte Stack - der
 * HTTPS-Handshake fuer den Firmware-Download braucht wie beim
 * Kalender-Download etwas mehr Stack als der Rest der Anwendung, siehe
 * kalender_anzeige.c). Einmalig aus app_main() aufzurufen, nachdem
 * netz_start() lief. */
void ota_starten(void);

/* True, sobald die periodische Pruefung eine Version gefunden hat, die von
 * der laufenden abweicht - Grundlage fuer das Update-Symbol auf dem
 * Hauptbildschirm und fuer den Update-Button im Einstellungen-Menue. */
bool ota_update_verfuegbar(void);

/* Versionsnummer der bereitstehenden bzw. der laufenden Firmware (fuer die
 * Beschriftung im Einstellungen-Menue). Zeiger bleibt gueltig. */
const char *ota_verfuegbare_version(void);
const char *ota_laufende_version(void);

/* Stoesst die Installation an. Kehrt SOFORT zurueck - der eigentliche
 * Download laeuft im OTA-Hintergrund-Task (darf nie im LVGL-Task passieren,
 * siehe FALLSTRICKE #16/#19) und startet das Geraet danach neu. Der
 * Fortschritt ist ueber ota_laeuft()/ota_fortschritt_prozent() sichtbar. */
void ota_installation_anstossen(void);

/* Fragt die Liste der im Download-Repo veroeffentlichten Versionen ueber die
 * GitHub-API ab (neueste zuerst, Entwuerfe/Vorabversionen ausgelassen) und
 * liefert deren Anzahl. Dauert einige Sekunden und braucht WLAN - deshalb
 * bewusst nur beim Oeffnen des Einstellungen-Menues aufrufen, nicht
 * periodisch (die API erlaubt unangemeldet 60 Abfragen pro Stunde).
 * Danach stehen die Namen ueber ota_version_name() bereit. */
int ota_versionen_abfragen(void);
int ota_versionen_anzahl(void);
const char *ota_version_name(int index);

/* Stoesst eine Auffrischung der Liste im HINTERGRUND-Task an und kehrt
 * sofort zurueck - aus der Oberflaeche immer diese Variante verwenden,
 * niemals ota_versionen_abfragen() direkt (die telefoniert und wuerde den
 * LVGL-Task fuer Sekunden blockieren). */
void ota_versionen_auffrischen(void);

/* Stoesst eine SOFORTIGE Pruefung samt Versionsliste im Hintergrund-Task an
 * und kehrt sofort zurueck. Fuer das Einstellungen-Menue gedacht: die
 * regulaere Pruefung laeuft erst 60 s nach dem Boot, wer das Menue ueber das
 * Zahnrad des Startbildschirms oeffnet, ist also fast immer zu frueh dran und
 * saehe weder Update-Knopf noch Versionsliste.
 *
 * ota_pruefung_laeuft() meldet, ob noch gewartet wird - taugt fuer ein
 * "Suche nach Updates..." in der Oberflaeche. */
void ota_pruefung_anstossen(void);
bool ota_pruefung_laeuft(void);

/* Installiert gezielt die angegebene Version - auch eine AELTERE als die
 * laufende (Peters Fall: eine Version gefaellt nicht, ein Feature daraus
 * spaeter aber doch). Verhaelt sich sonst wie
 * ota_installation_anstossen(): kehrt sofort zurueck, der Download laeuft
 * im Hintergrund-Task und startet das Geraet danach neu.
 *
 * Anders als das Zurueckschalten per ota_auf_vorherige_version_wechseln()
 * ist das NICHT auf die zwei App-Partitionen beschraenkt - die Datei wird
 * frisch geladen, es kommt also jede veroeffentlichte Version in Frage. */
void ota_version_installieren(const char *version);

/* Version in der ZWEITEN App-Partition, also die zuvor laufende. Das Board
 * hat genau zwei App-Partitionen (ota_0/ota_1, siehe partitions.csv) - mehr
 * als eine Vorgaengerversion kann prinzipbedingt nicht vorgehalten werden,
 * eine Versions-Auswahlliste ist daher nicht moeglich.
 * Liefert false, wenn der Slot noch leer oder das Image als ungueltig
 * markiert ist (dann waere ein Zurueckschalten aussichtslos). */
bool ota_vorherige_version(char *puffer, size_t puffer_groesse);

/* Schaltet die Boot-Partition auf die vorherige Version um. Der Aufrufer
 * muss danach neu starten (esp_restart), damit sie wirksam wird. */
esp_err_t ota_auf_vorherige_version_wechseln(void);

#endif
