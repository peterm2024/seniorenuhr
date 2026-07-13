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

**Parser-Tests auf dem PC ausführen:**

```powershell
test_host\teste.ps1
```

**LVGL-Fonts neu generieren** (nur nötig, wenn Größen/Zeichen geändert werden):

```powershell
tools\fonts\erzeuge_fonts.ps1
```

## Projektstruktur

```
├── CMakeLists.txt           ESP-IDF-Projekt (Wurzel)
├── sdkconfig.defaults       Board-Konfiguration (8 MB Flash!, Octal-PSRAM)
├── partitions.csv           Partitionslayout
├── FAHRPLAN.md              Architektur & Phasenplan
├── ENTWICKLUNG.md           diese Datei
├── main/                    Firmware: app_main.c, anzeige.c (Display/LVGL)
├── components/
│   └── kalender/            ICS-Parser (portables C, läuft auf PC und ESP32)
├── test_host/               PC-Tests für den Parser (25 Prüfungen)
├── assets/fonts/            generierte LVGL-Fonts mit Umlauten (Montserrat-Bold, OFL-Lizenz)
│                              schrift_uhr_128   – Uhrzeit (nur Ziffern + :)
│                              schrift_gross_72  – Wochentag
│                              schrift_mittel_40 – Datum, Termine
│                              schrift_klein_28  – Beschriftungen
└── tools/fonts/             Generator-Skript (lädt die TTF bei Bedarf selbst)
```

**Achtung Hardware-Falle:** Das Board meldet sich als N8R8-Variante —
**8 MB Flash** (nicht 16, wie die Produktseite verspricht) und 8 MB PSRAM.
Bei falscher Flash-Größe bootet es in einer Assert-Schleife
(`init_flash ... flash_ret == ESP_OK`).

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
