# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Kalender-Uhr für hochbetagte Menschen auf dem Waveshare ESP32-S3-Touch-LCD-7.
ESP-IDF 5.5 + LVGL 9. **Kommunikation, Dokumentation und Code-Kommentare auf
Deutsch** (auch Log-Ausgaben; nur die Oberfläche ist zweisprachig).

## Zuerst lesen

`FALLSTRICKE_UND_WORKAROUNDS.md` enthält 43 gelöste Probleme mit Ursache und
Lösung. **Vor jeder Untersuchung eines Fehlers dort nachschauen** — vieles war
schon einmal da, und mehrere Einträge beschreiben Fallen, in die man sonst
zuverlässig erneut tappt. `FAHRPLAN.md` hat Architektur und
Entwurfsentscheidungen, `ENTWICKLUNG.md` die Umgebung.

## Befehle

ESP-IDF-Umgebung einmal pro PowerShell-Sitzung aktivieren (schlägt fehl, wenn
eine Python-Virtualenv aktiv ist):

```powershell
Remove-Item Env:VIRTUAL_ENV -ErrorAction SilentlyContinue
. $env:USERPROFILE\esp\esp-idf\export.ps1
```

```powershell
idf.py build
idf.py -p COM3 flash
idf.py -p COM3 monitor          # Beenden mit Strg+]
test_host\teste.ps1             # alle Host-Tests (93 Prüfungen)
tools\fonts\erzeuge_fonts.ps1   # nur bei geänderten Schriftgrößen/Zeichen
```

Einzelne Testsuite direkt starten (nach `teste.ps1`, das sie baut):
`test_host\test_ics.exe`, `test_host\test_version.exe`,
`test_host\test_protokoll.exe` (letzterer legt Dateien an — aus einem
temporären Verzeichnis starten).

Produktions-Build (ohne Entwicklungswerkzeuge, eigenes Build-Verzeichnis,
damit der Dev-Build daneben bestehen bleibt):

```powershell
$env:PRODUKTIONS_BUILD = "1"; idf.py -B build_prod -p COM5 flash
```

Absturzprotokoll nach einem Crash (ELF muss zur geflashten Firmware passen,
für das Eltern-Board also `-B build_prod` mitgeben):

```powershell
idf.py -p COM5 coredump-info
```

Release: `git tag vX.Y.Z && git push --tags` löst
`.github/workflows/release.yml` aus. Der Workflow baut mit Platzhalter-
Zugangsdaten und veröffentlicht die Binary in einem **separaten** Repo
(`peterm2024/seniorenuhr-firmware`), aus dem das Gerät per OTA lädt.

## Zwei Boards, nicht verwechseln

- **COM3** = Board 1, Entwicklungsboard.
- **COM5** = Board 2, läuft produktiv bei den Eltern (weit entfernt).

Auf Board 2 **niemals `erase_flash`** — das löscht WLAN-Zugangsdaten,
Kalender-Adresse und die Tabletten-Bestätigungen. Normales `idf.py flash`
lässt NVS und die `speicher`-Partition unangetastet. Bei zwei gleichzeitig
angeschlossenen Boards ist die Verwechslungsgefahr real (siehe Fallstrick 39).

Serielle Ports finden: `Get-PnpDevice -Class Ports` oder
`[System.IO.Ports.SerialPort]::GetPortNames()`. **`Win32_SerialPort` sieht die
CH343-Adapter nicht.**

## Architektur

**Zwei Tasks, strikt getrennt.** Der LVGL-Task aktualisiert die Anzeige im
Sekundentakt (`uhr_tick` in `app_main.c`), ein eigener Kalender-Task
(`kalender_anzeige.c`) erledigt Download und Parsen. Netzwerkprobleme können
die Anzeige dadurch nie ins Stocken bringen. **Grundregel des Projekts: die
Anzeige hat Vorrang vor allem anderen** — lieber eine ungenaue Uhrzeit als ein
dunkler Bildschirm.

**Interner SRAM ist die knappe Ressource, nicht PSRAM.** Im Betrieb sind rund
31 KB frei, größter zusammenhängender Block ~11 KB. Task-Stacks und TLS-Puffer
müssen dorthin. Fallstrick 39: ein dauerhaft laufender Webserver + mDNS banden
17 KB und ließen die Namensauflösung scheitern — Ursache monatelang
unerklärter GitHub-Abbrüche. **Jede Wartungsfunktion ist deshalb standardmäßig
AUS und wird nur auf Zuruf eingeschaltet.** Große Puffer gehören per
`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` ins PSRAM — aber nur, wenn der Code
nicht in den Flash schreibt (Flash-Schreibzugriffe schalten den Cache ab, über
den PSRAM angebunden ist, siehe Fallstrick 25).

**LVGL hat einen eigenen 64-KB-Pool, getrennt vom Heap.** Beim Suchen von
Speicherproblemen immer beides getrennt ausweisen (`lv_mem_monitor()` vs.
`heap_caps_get_free_size`), sonst sucht man an der falschen Stelle.

**Jeder LVGL-Aufruf aus einem fremden Task muss in `lvgl_port_lock(0)` /
`lvgl_port_unlock()`.** Der Lock ist rekursiv (`xSemaphoreTakeRecursive`),
verschachtelte Aufrufe sind also unproblematisch. Wo eine Reihenfolge für die
Optik zwingend ist, muss sie durch einen gemeinsamen Lock erzwungen werden —
sonst zeichnet der LVGL-Task dazwischen (Fallstrick 42).

**Der ICS-Parser (`components/kalender`) ist portabel** und läuft unverändert
auf dem PC. Alles, was ohne Hardware prüfbar ist, gehört dorthin oder in eine
eigene Datei unter `main/` mit den Stubs aus `test_host/stubs/` — so entstanden
`version_vergleich.c` und `tabletten_protokoll.c`. Das ist oft der einzige
Weg, etwas zu testen, das sonst nur um Mitternacht oder bei einem echten
Download passiert.

**Oberflächentexte laufen über `texte.c/h`** (`text(TXT_...)`), Deutsch als
Referenzsprache mit automatischem Rückfall. Ausgenommen: Log-Ausgaben,
Kommentare und die Webkonfig-HTML. Die Tabletten-Präfixe im Kalender
(`TABLETTE:`, `PILL:`, …) sind **sprachunabhängig** — ein Sprachwechsel darf
bestehende Kalendereinträge nie entwerten.

**Zugangskonzept:** Die Eltern sollen nie versehentlich in die Einstellungen
geraten. Das Menü ist nur über 5 Sekunden Halten plus Bestätigungsdialog
erreichbar (`menue_halten_cb` in `app_main.c`, hängt am Update-Symbol und an
der Status-Tippfläche).

## Fallen, die der Compiler oder die Plattform stellt

- **`-Werror=format-truncation`**: Bei `snprintf` mit einem Array-Feld hinter
  einem Zeiger (`e->titel`) verliert GCC die bekannte Größe und nimmt den
  Worst Case an. Immer explizite Präzision verwenden:
  `snprintf(ziel, N, "%.*s", N - 1, quelle->feld)`.
- **FatFs läuft ohne lange Dateinamen** (`CONFIG_FATFS_LFN_NONE`). Dateinamen
  auf der `speicher`-Partition müssen dem 8.3-Format genügen —
  `tabletten.txt` wird abgelehnt, `tablette.txt` nicht.
- **Der Host-Compiler ist strenger als der ESP-Build** (dort sind einige
  Warnungen entschärft). Code, der für das Gerät baut, kann im Host-Test an
  `-Wunused-but-set-variable` scheitern.
- **8 MB Flash, nicht 16** (N8R8-Variante, entgegen der Produktseite). Bei
  falscher Flash-Größe bootet das Board in einer Assert-Schleife.

## Geheimnisse

`main/secrets.h` (WLAN-Zugang, private Kalender-Adresse) ist gitignoriert und
darf nie ins Repo. Neue Arbeitskopie: `copy main\secrets.example.h
main\secrets.h`. Der Release-Workflow setzt die Platzhalter-Variante ein, damit
veröffentlichte Binaries strukturell nichts Privates enthalten können.

`screenshots_lokal/` ist bewusst gitignoriert — dort landet ungefiltert alles,
was auf dem Schirm stand, auch echte Termine und Tablettennamen. Was in die
Doku soll, wird von Hand nach `docs/screenshots/` kopiert.

## Arbeitsweise in diesem Projekt

- **Peters Beobachtungen ernst nehmen und verifizieren.** Mehrfach lag eine
  bequeme Erklärung daneben und seine Beobachtung war präzise richtig (siehe
  die Lehren in Fallstrick 19, 28, 41, 42, 43). Messen statt vermuten.
- **Temporäre Instrumentierung ist das Mittel der Wahl**, wenn sich etwas
  nicht anders beweisen lässt — vor dem Commit wieder entfernen und das
  Ergebnis im Commit oder in `FALLSTRICKE_UND_WORKAROUNDS.md` festhalten.
- **Ein Test muss den Fehlerfall erzeugen, nicht den Normalfall.** Eine
  Prüfung, die auch ohne den Fix grün wäre, beweist nichts.
- Touch-Bedienung kann nur Peter testen. Screenshots holt
  `tools/screenshot_flash_abholen.py COM3` aus der Flash-Partition (der Auslöser
  ist ein Knopf auf dem Gerät, seriell nicht auslösbar).
