# Entwicklungsumgebung

Was auf diesem Rechner eingerichtet ist und wie man damit arbeitet
(Stand: 10.07.2026, Windows 11).

## Installiert

| Werkzeug | Ort / Version | Zweck |
|---|---|---|
| ESP-IDF v5.5 | `C:\Users\peter\esp\esp-idf` | Firmware bauen & flashen |
| ESP-IDF-Toolchains | `C:\Users\peter\.espressif` (xtensa-gcc, CMake, Ninja, OpenOCD, Python-Umgebung) | wird von ESP-IDF genutzt |
| VS Code-Extension | `espressif.esp-idf-extension` | Bauen/Flashen/Monitor per Klick |
| gcc 16 (WinLibs) | über winget installiert | ICS-Parser-Tests auf dem PC |
| Node.js 22 | vorhanden | Font-Generierung (`lv_font_conv`) |
| GitHub CLI | angemeldet als `peterm2024` | Repo-Verwaltung |

## Tägliche Befehle

**ESP-IDF-Umgebung aktivieren** (pro PowerShell-Sitzung einmal):

```powershell
. $env:USERPROFILE\esp\esp-idf\export.ps1
```

Danach stehen `idf.py build`, `idf.py -p COMx flash monitor` usw. zur Verfügung.
Alternativ in VS Code: Befehlspalette → „ESP-IDF: Configure ESP-IDF Extension"
→ „Use existing setup" (findet die Installation automatisch).

**Firmware bauen und aufs Board spielen** (Board hängt an **COM3**):

```powershell
idf.py build
idf.py -p COM3 flash
idf.py -p COM3 monitor    # serielle Ausgabe ansehen, Beenden mit Strg+]
```

**Absturzprotokoll (Core-Dump) auslesen** — nach einem Absturz, sobald das
Board wieder am Rechner hängt:

```powershell
idf.py -p COM5 coredump-info      # Backtrace + Task-Zustand im Klartext
idf.py -p COM5 coredump-debug     # dasselbe interaktiv in gdb
```

Der Dump liegt seit 09.08.2026 in einer eigenen Flash-Partition (`coredump`,
siehe `partitions.csv`) statt nur auf der seriellen Leitung. Er wird im
Moment des Absturzes geschrieben und übersteht Neustart und Stromausfall —
das Board kann also ohne Kabel irgendwo betrieben werden (Powerbank am
Router), und das Protokoll wird beim nächsten Einstecken ausgelesen. Der
Dump bleibt stehen, bis ihn der nächste Absturz überschreibt. Löschen von
Hand (ESP-IDF 5.5 kennt kein `coredump-erase`, die Partition liegt laut
`partitions.csv` bei `0x650000` und ist 256 KB groß):

```powershell
python -m esptool -p COM5 erase_region 0x650000 0x40000
```

Wichtig: Die ausgelesene ELF-Datei muss zur geflashten Firmware passen. Für
das Eltern-Board also `-B build_prod` mit angeben, sonst zeigen die
Adressen ins Leere.

**Tests auf dem PC ausführen** (brauchen kein Board):

```powershell
test_host\teste.ps1
```

Baut und startet drei Suiten, zusammen 120 Prüfungen:

| Suite | Prüft | Warum ohne Hardware |
|---|---|---|
| `test_ics` (35) | ICS-Parser (`components/kalender`) | portabel geschrieben, läuft unverändert auf dem PC |
| `test_version` (20) | Versionsvergleich für die Update-Prüfung | sonst nur über echte Downloads prüfbar |
| `test_protokoll` (65) | Tabletten-Langzeitprotokoll | schreibt nur beim Mitternachtswechsel — auf dem Board hieße das warten |

Module aus `main/`, die ESP-Header einbinden, werden dafür gegen die Stubs in
`test_host/stubs/` gebaut (siehe `teste.ps1`). Dieselben Suiten laufen bei
jedem Push in der CI (`.github/workflows/test.yml`).

**LVGL-Fonts neu generieren** (nur nötig, wenn Größen/Zeichen geändert werden):

```powershell
tools\fonts\erzeuge_fonts.ps1
```

## Projektstruktur

```
├── CMakeLists.txt           ESP-IDF-Projekt (Wurzel)
├── sdkconfig.defaults       Board-Konfiguration (Flash auf 8 MB, Octal-PSRAM)
├── partitions.csv           Partitionslayout
├── FAHRPLAN.md              Architektur & Phasenplan
├── ENTWICKLUNG.md           diese Datei
├── main/                    Firmware: app_main.c, anzeige.c (Display/LVGL)
├── components/
│   └── kalender/            ICS-Parser (portables C, läuft auf PC und ESP32)
├── test_host/               PC-Tests ohne Hardware (120 Prüfungen, siehe unten)
├── assets/fonts/            generierte LVGL-Fonts mit Umlauten (Montserrat-Bold, OFL-Lizenz)
│                              schrift_uhr_128   – Uhrzeit (nur Ziffern + :)
│                              schrift_gross_72  – Wochentag
│                              schrift_mittel_40 – Datum, Termine
│                              schrift_klein_28  – Beschriftungen
└── tools/fonts/             Generator-Skript (lädt die TTF bei Bedarf selbst)
```

**Achtung Hardware-Falle:** Das Board gibt es in zwei Flash-Varianten. Das
Entwicklungsboard ist ein **N8R8 mit 8 MB**, das zweite Board ein **N16R8 mit
16 MB**; 8 MB PSRAM haben beide. Konfiguriert ist 8 MB, damit dieselbe Binary
auf beiden läuft. Ist die konfigurierte Flash-Größe größer als der verbaute
Chip, bootet das Board in einer Assert-Schleife
(`init_flash ... flash_ret == ESP_OK`) — die Variante deshalb immer per
`esptool flash_id` prüfen.

## Bekannte Fallstricke

Gelöste Probleme mit Ursache und Lösung stehen in **[FALLSTRICKE_UND_WORKAROUNDS.md](FALLSTRICKE_UND_WORKAROUNDS.md)** —
dort nachschauen, bevor ein schon einmal gelöstes Problem erneut untersucht wird.

## Wenn die Hardware da ist (Phase 0)

1. Board per USB-C anschließen, im Geräte-Manager den COM-Port merken
2. Waveshare-Demo testen: [Wiki ESP32-S3-Touch-LCD-7](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7)
   → Abschnitt „ESP-IDF" → LVGL-Beispiel bauen und flashen
3. Danach das eigentliche Firmware-Projekt anlegen (`main/`, `CMakeLists.txt` im
   Wurzelverzeichnis) — die Komponente `kalender` und die Fonts werden dann
   direkt mit eingebunden

## Stolperfallen

- `install.ps1`/`export.ps1` verweigern den Dienst, wenn eine Python-
  Virtualenv aktiv ist (Meldung „called from a virtual environment").
  Lösung: vorher `Remove-Item Env:VIRTUAL_ENV` ausführen.
- Beim ESP-IDF handelt es sich um einen platzsparenden Shallow-Klon; die
  Warnung „Git version unavailable" von `idf.py` ist deshalb normal.
