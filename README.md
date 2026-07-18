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

## Lizenz

Der Quellcode dieses Projekts steht unter der **MIT-Lizenz** (siehe [LICENSE](LICENSE)).

Ausnahme: die Schriftdateien in `assets/fonts/` sind aus der Schrift
[Montserrat](https://github.com/JulietaUla/Montserrat) generiert und stehen unter der
**SIL Open Font License 1.1** (siehe [assets/fonts/LICENSE-OFL.txt](assets/fonts/LICENSE-OFL.txt)).

Fremdkomponenten (ESP-IDF, LVGL, esp_lvgl_port, GT911-Touchtreiber) sind nicht Teil
dieses Repositories, sondern werden beim Bauen über den ESP-IDF-Komponentenmanager
bezogen; sie stehen unter ihren eigenen permissiven Lizenzen (Apache 2.0 bzw. MIT).
