# Seniorenuhr

Eine Kalender-Uhr für Senioren auf Basis des **Waveshare ESP32-S3-Touch-LCD-7**
(7"-Touchdisplay, 800×480).

Die Uhr zeigt dauerhaft und ohne jede Bedienung:

- **Wochentag** (groß, ausgeschrieben), Uhrzeit, Datum und Tageszeit in Worten
- die **heutigen Termine** aus einem Google-Kalender (Pflege aus der Ferne per Handy/Notebook)
- den **Tablettenplan** für heute — auf Wunsch per Touch abhakbar

Projektstand, Architektur und Bauplan: siehe **[FAHRPLAN.md](FAHRPLAN.md)**.

## So sieht es aus

Die Hauptanzeige — links die antippbaren Wochentage, oben Wochentag und große Uhr,
darunter Tabletten- und Terminplan des Tages. Oben rechts drei Statussymbole
(WLAN, Zeit-Synchronisation, Kalender). Nachts dunkel, tagsüber hell:

<img src="docs/screenshots/hauptanzeige_2026-07-21_proportional.png" alt="Hauptanzeige der Seniorenuhr" width="640">

| Tagesansicht (Tabletten abhaken) | Startbildschirm (Boot) |
|---|---|
| <img src="docs/screenshots/tagesansicht_2026-07-22_proportional.png" alt="Tagesansicht mit Tabletten-Schaltern" width="360"> | <img src="docs/screenshots/startbildschirm_2026-07-21_proportional.png" alt="Startbildschirm beim Booten" width="360"> |
| Antippen eines Wochentags/„Heute“ öffnet den Tagesdialog: die drei Einnahmen (Früh/Mittag/Abend) lassen sich per Kippschalter abhaken. | Während des Startens zeigen drei Ringe den Fortschritt: WLAN-Verbindung, Zeit-Synchronisation (NTP) und erster Kalender-Abruf. |

> Alle Screenshots entstehen mit dem eingebauten Bildschirmfoto-Werkzeug
> (`main/screenshot_debug.c`) und liegen unter
> **[docs/screenshots/](docs/screenshots/)** — Details dort in der README.

## Technik in Kürze

- ESP-IDF 5.x + LVGL, Uhrzeit per NTP (Zeitzone Europe/Berlin, automatische Sommerzeit)
- Termine als ICS-Abo per HTTPS aus Google Kalender, lokaler Cache auf SD-Karte
- Tabletten = wiederkehrende Kalendereinträge mit Präfix `TABLETTE:`
- OTA-Updates für Wartung aus der Ferne, komplett lautlos

## Selber bauen & aufs Gerät bringen

Gebaut und geflasht wird mit **ESP-IDF** — also mit dem Werkzeug `idf.py`,
**nicht** mit `make`. (ESP-IDF ruft darunter selbst CMake und Ninja auf; man
muss davon nichts von Hand bedienen.) Ausführliche Rechner-Einrichtung und die
täglichen Befehle stehen in **[ENTWICKLUNG.md](ENTWICKLUNG.md)** — hier die
Kurzfassung.

### Was man braucht

- **Hardware:** Waveshare ESP32-S3-Touch-LCD-7 (**N8R8-Variante: 8 MB Flash,
  8 MB PSRAM** — nicht die auf der Produktseite genannten 16 MB!), USB-C-Kabel,
  optional eine microSD-Karte für den Kalender-Cache
- **Software:** [ESP-IDF v5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/get-started/index.html)
  mit den Toolchains (Installer richtet alles ein). Fremdkomponenten (LVGL,
  esp_lvgl_port, GT911-Touchtreiber) zieht der ESP-IDF-Komponentenmanager beim
  ersten Bauen automatisch nach.

### 1. Holen und konfigurieren

```powershell
git clone https://github.com/peterm2024/seniorenuhr.git
cd seniorenuhr
```

Zugangsdaten eintragen — die Vorlage kopieren und ausfüllen (die echte
`secrets.h` ist per `.gitignore` ausgeschlossen und landet nie auf GitHub):

```powershell
copy main\secrets.example.h main\secrets.h
```

In `main\secrets.h` dann WLAN-Name, WLAN-Passwort und die private iCal-Adresse
aus Google Kalender eintragen. (WLAN und Kalender-Adresse lassen sich später
auch am Gerät selbst ändern, ohne neu zu flashen.)

### 2. Bauen und flashen

ESP-IDF-Umgebung in der PowerShell-Sitzung einmal aktivieren:

```powershell
. $env:USERPROFILE\esp\esp-idf\export.ps1
```

Dann bauen, aufs Board spielen (hier an **COM3** — je nach PC anderer Port,
siehe Geräte-Manager) und die serielle Ausgabe mitlesen:

```powershell
idf.py build
idf.py -p COM3 flash
idf.py -p COM3 monitor      # Beenden mit Strg+]
```

Beim allerersten Mal lohnt sich vorab das Waveshare-eigene LVGL-Beispiel als
Funktionstest der Hardware — Anleitung im
[Waveshare-Wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7).

> **Board bootet in einer Assert-Schleife?** Fast immer die falsch erkannte
> Flash-Größe (16 statt 8 MB). Ursache/Lösung und weitere schon gelöste
> Probleme stehen in **[FALLSTRICKE_UND_WORKAROUNDS.md](FALLSTRICKE_UND_WORKAROUNDS.md)**.

## Wichtiger Hinweis: kein Medizinprodukt, keine Garantie

Dieses Projekt ist ein **privates Hobbyprojekt** — eine Kalender-Uhr mit
Erinnerungsanzeige. Es ist **kein Medizinprodukt** im Sinne der
EU-Medizinprodukteverordnung (MDR), hat keine medizinische Zweckbestimmung und
ersetzt weder Pflege noch ärztliche Betreuung noch geprüfte
Medikamenten-Erinnerungssysteme.

Die Anzeige kann jederzeit ausfallen (Stromausfall, WLAN-Störung,
Softwarefehler o. Ä.). **Verlasst euch bei kritischen Medikamenten niemals
allein auf dieses Gerät** — sichert die Einnahme immer zusätzlich ab
(z. B. Dosierbox, Anruf, Pflegedienst).

Die Nutzung erfolgt auf eigene Gefahr. Gewährleistung und Haftung sind im
rechtlich zulässigen Umfang ausgeschlossen (siehe Abschnitte 15–17 der
GPLv3-Lizenz).

## Lizenz

Der Quellcode dieses Projekts steht unter der **GNU General Public License v3**
(siehe [LICENSE](LICENSE)). Das heißt: Jeder darf ihn frei nutzen, verändern und
weitergeben — wer ihn aber (auch verändert oder in Geräten) weitergibt, muss den
Quellcode unter denselben Bedingungen offenlegen. Copyright (c) 2026 peterm2024.

Ausnahme: die Schriftdateien in `assets/fonts/` sind aus der Schrift
[Montserrat](https://github.com/JulietaUla/Montserrat) generiert und stehen unter der
**SIL Open Font License 1.1** (siehe [assets/fonts/LICENSE-OFL.txt](assets/fonts/LICENSE-OFL.txt)).

Fremdkomponenten (ESP-IDF, LVGL, esp_lvgl_port, GT911-Touchtreiber) sind nicht Teil
dieses Repositories, sondern werden beim Bauen über den ESP-IDF-Komponentenmanager
bezogen; sie stehen unter ihren eigenen permissiven Lizenzen (Apache 2.0 bzw. MIT).
