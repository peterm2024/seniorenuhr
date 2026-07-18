# Seniorenuhr

Eine Kalender-Uhr für Senioren auf Basis des **Waveshare ESP32-S3-Touch-LCD-7**
(7"-Touchdisplay, 800×480).

Die Uhr zeigt dauerhaft und ohne jede Bedienung:

- **Wochentag** (groß, ausgeschrieben), Uhrzeit, Datum und Tageszeit in Worten
- die **heutigen Termine** aus einem Google-Kalender (Pflege aus der Ferne per Handy/Notebook)
- den **Tablettenplan** für heute — auf Wunsch per Touch abhakbar

Projektstand, Architektur und Bauplan: siehe **[FAHRPLAN.md](FAHRPLAN.md)**.

## Technik in Kürze

- ESP-IDF 5.x + LVGL, Uhrzeit per NTP (Zeitzone Europe/Berlin, automatische Sommerzeit)
- Termine als ICS-Abo per HTTPS aus Google Kalender, lokaler Cache auf SD-Karte
- Tabletten = wiederkehrende Kalendereinträge mit Präfix `TABLETTE:`
- OTA-Updates für Wartung aus der Ferne, komplett lautlos

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
