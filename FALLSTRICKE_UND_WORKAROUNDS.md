# Entwicklungs-Logbuch: Gelöste Fallstricke & Workarounds

Dieses Dokument dokumentiert die technischen Hürden, die während der Entwicklung der Seniorenuhr aufgetreten sind, sowie die erarbeiteten Lösungen und Best Practices.

---

## 1. ESP-IDF `install.ps1`/`export.ps1` verweigern den Dienst innerhalb einer aktiven Python-Virtualenv

**Problem:** Sowohl die Werkzeug-Installation (`install.ps1 esp32s3`) als auch das spätere Aktivieren der Umgebung (`export.ps1`) brachen mit `ERROR: This script was called from a virtual environment, can not create a virtual environment again` ab.
**Ursache:** Die PowerShell-Sitzung dieser Maschine startet mit einer bereits aktivierten Python-Virtualenv (`$env:VIRTUAL_ENV` zeigt auf ein fremdes venv, z. B. eines Hilfswerkzeugs). ESP-IDFs eigenes Setup will selbst ein venv unter `~/.espressif` anlegen und bricht ab, sobald es eine bereits aktive Virtualenv erkennt.
**Lösung:** Vor jedem `install.ps1`/`export.ps1`-Aufruf `Remove-Item Env:VIRTUAL_ENV -ErrorAction SilentlyContinue` ausführen. Gilt für **jede neue Shell-Sitzung**, die mit ESP-IDF arbeitet — steht auch in `ENTWICKLUNG.md`.

---

## 2. Board bootete in einer Assert-Schleife — Flash-Chip ist 8 MB, nicht 16 MB wie beworben

**Problem:** Nach dem ersten Flash-Vorgang mit `sdkconfig.defaults` auf 16 MB Flash bootete das Board endlos mit `assert failed: __esp_system_init_fn_init_flash startup_funcs.c:118 (flash_ret == ESP_OK)`.
**Ursache:** Die Waveshare-Produktseite nennt 16 MB Flash für das ESP32-S3-Touch-LCD-7 — das konkret gelieferte Board ist aber die **N8R8-Variante mit 8 MB Flash** (verifiziert per `python -m esptool --chip esp32s3 -p COM3 flash_id`: GigaDevice-Chip, 8 MB, Quad-Modus). Die Firmware versuchte, eine Flash-Größe zu adressieren, die physisch nicht vorhanden ist.
**Lösung:** `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y` statt `_16MB` in `sdkconfig.defaults`, Partitionstabelle (`partitions.csv`) entsprechend auf 8 MB angepasst, `sdkconfig` gelöscht und neu erzeugt.
**Lehre:** Bei Waveshare-Boards (und generell bei Klonen/Werksvarianten) die tatsächliche Chip-Bestückung immer per `esptool flash_id` gegenprüfen, statt der Produktseite blind zu vertrauen — insbesondere bevor die Flash-Größen-Konfiguration gesetzt wird.

---

## 3. Eigene Test-Erwartung war falsch, nicht der Parser

**Problem:** Beim ersten Testlauf von `test_ics_parser.c` schlug die Prüfung „vier Einträge am 15.07." fehl — der Parser lieferte nur drei.
**Ursache:** Beim Schreiben der Testdaten und der zugehörigen Erwartung wurde ein Termin falsch mitgezählt (die Erwartung ging von vier Einträgen aus, tatsächlich treffen laut Testkalender nur drei auf diesen Tag zu). Der Parser selbst arbeitete korrekt.
**Lösung:** Testerwartung von 4 auf 3 korrigiert (`test_ics_parser.c`), alle 25 Prüfungen liefen danach grün.
**Lehre:** Testerwartungen sind genauso fehleranfällig wie der Code selbst. Bei einer Diskrepanz zwischen erwartetem und tatsächlichem Ergebnis zuerst von Hand nachrechnen (hier: die Testdaten Zeile für Zeile durchgehen), statt vorschnell einen Bug im Parser zu vermuten.

---

## 4. `git commit -m` mit mehrzeiliger Nachricht scheiterte in PowerShell an Anführungszeichen im Text

**Problem:** Ein `git commit -m @'...'@`-Aufruf mit einer mehrzeiligen Commit-Message, die doppelte Anführungszeichen enthielt (z. B. wörtlich zitierte Anzeige-Ausgabe wie `"Montag 21:03..."`), scheiterte mit mehreren `error: pathspec '...' did not match any file(s)` — Git interpretierte Teile der Nachricht als zusätzliche Datei-Argumente.
**Ursache:** PowerShell reicht Strings mit eingebetteten doppelten Anführungszeichen beim Aufruf nativer Programme (wie `git.exe`) nicht immer als ein einziges Argument durch — an den Anführungszeichen wird die Zeichenkette neu tokenisiert, wodurch `-m` nur noch einen Teil der Nachricht bekommt und der Rest als lose Wörter (vermeintliche Dateipfade) ankommt.
**Lösung:** Commit-Nachricht stattdessen in eine temporäre Datei im Scratchpad-Ordner schreiben und `git commit -F <datei>` verwenden — das umgeht die Argument-Requotierung vollständig.
**Regel:** Sobald eine Commit-Message doppelte Anführungszeichen enthalten könnte (zitierte Ausgaben, Produktnamen in "Anführungszeichen"), grundsätzlich `-F <datei>` statt `-m` verwenden.

---

## 5. `esp_netif_sntp_init`/`ESP_NETIF_SNTP_DEFAULT_CONFIG` — Header- und Komponentennamen sind API-Version-abhängig

**Problem:** Vor dem ersten Build war unklar, ob `esp_netif_sntp.h` (moderne ESP-IDF-5.x-API) oder das ältere `esp_sntp.h`/`sntp_*`-API verwendet werden muss.
**Ursache:** ESP-IDF hat die SNTP-API über mehrere Versionen hinweg umgebaut; ältere Tutorials/Beispiele im Netz nutzen noch `sntp_setoperatingmode()`/`sntp_init()` direkt.
**Lösung:** Für ESP-IDF 5.5 die moderne, empfohlene API aus `esp_netif_sntp.h` verwendet (`ESP_NETIF_SNTP_DEFAULT_CONFIG`, `esp_netif_sntp_init`, `sync_cb`-Callback) — kompilierte und funktionierte im ersten Anlauf ohne Anpassung.
**Lehre:** Bei ESP-IDF-Netzwerk-APIs immer die zur installierten IDF-Version (hier: v5.5.4, siehe `ENTWICKLUNG.md`) passende Doku/Header prüfen, nicht die erstbeste Suchtreffer-Version übernehmen.
