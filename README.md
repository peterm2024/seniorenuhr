# Seniorenuhr

**🇬🇧 English:** the interface is available in German and English (further
languages on demand) — see the [English summary](#english-summary) at the end
of this file. Documentation and source comments are in German.

Eine Kalender-Uhr für hochbetagte Menschen, gebaut auf dem Waveshare
ESP32-S3-Touch-LCD-7 (7-Zoll-Touchdisplay, 800 × 480). Das Gerät steht dauerhaft
in der Wohnung und zeigt Wochentag, Uhrzeit, Datum sowie die heutigen Termine
und den Tablettenplan. Gepflegt wird alles aus der Ferne über einen gewöhnlichen
Kalender (Google Kalender oder Nextcloud) — am Gerät selbst muss niemand etwas
bedienen. Entstanden ist das Projekt für die eigenen Eltern.

<img src="docs/screenshots/geraet_2026-08-18.jpg" alt="Die Seniorenuhr im Betrieb, in einem 3D-gedruckten Gehäuse auf einem Tisch" width="620">

*Das fertige Gerät im Alltag. Links die antippbaren Wochentage, rechts oben die
Statussymbole für WLAN, Zeitsynchronisation und Kalenderabruf — darunter die
Tabletten des Tages, abgehakte in Grau.*

Die folgenden Abbildungen sind direkt vom Gerät aufgenommene Bildschirmfotos
(siehe [docs/screenshots/](docs/screenshots/)):

<img src="docs/screenshots/hauptanzeige_2026-07-21_proportional.png" alt="Hauptanzeige der Seniorenuhr" width="640">

## Funktionsweise

Termine und Tablettenzeiten werden in einem normalen Kalender gepflegt, der eine
private ICS-Abo-Adresse bereitstellt. Die Uhr lädt diesen Kalender alle
15 Minuten per HTTPS, parst die Einträge und hält sie in einem lokalen Cache —
fällt das Internet aus, zeigt sie die zuletzt bekannten Daten weiter an.

Tabletten sind gewöhnliche wiederkehrende Kalendereinträge mit dem Präfix
`TABLETTE:`. Die Uhr erkennt das Präfix, führt diese Einträge in einer eigenen
Liste und lässt sie per Fingertipp abhaken; der Abhak-Status wird lokal
gespeichert und übersteht auch einen Neustart.

Erkannt werden neben `TABLETTE:`/`TABLETTEN:` auch die englischen Varianten
`PILL:`, `PILLS:` und `MED:` — und zwar **immer**, unabhängig von der
eingestellten Oberflächensprache. Ein Sprachwechsel darf bestehende
Kalendereinträge nie entwerten.

Die Uhrzeit kommt per NTP (Zeitzone Europe/Berlin, Sommerzeit automatisch). Das
Board hat keine batteriegepufferte Echtzeituhr — nach einem Stromausfall holt
sich die Uhr die Zeit über das WLAN zurück.

## Gestaltung

Die Anzeige folgt Regeln, die sich bei Uhren für demente und hochbetagte
Menschen bewährt haben:

- Der ausgeschriebene Wochentag steht zuoberst — die häufigste Frage ist
  „Welcher Tag ist heute?"
- Die Tageszeit steht zusätzlich in Worten da („Vormittag", „Abend"), weil
  07:00 und 19:00 auf Digitaluhren leicht zu verwechseln sind
- Hoher Kontrast, keine Animationen, nichts blinkt
- Abends wird die Anzeige gedimmt, nachts ist der Hintergrund schwarz und die
  Terminliste ausgeblendet; eine Berührung schaltet für 30 Sekunden auf volle
  Helligkeit zurück
- Fällige Tabletten werden farblich hervorgehoben statt mit einem Alarmton

| Tagesansicht | Startbildschirm |
|---|---|
| <img src="docs/screenshots/tagesansicht_2026-08-18_proportional.png" alt="Tagesansicht mit Tabletten-Checkboxen" width="360"> | <img src="docs/screenshots/startbildschirm_2026-07-21_proportional.png" alt="Startbildschirm beim Booten" width="360"> |
| *Der „Heute"-Dialog: fällige Tabletten werden per Checkbox angehakt, „OK" übernimmt die Änderungen erst nach Bestätigung.* | *Beim Start zeigen drei Ringe den Fortschritt: WLAN, Zeitabgleich, Kalenderabruf.* |

<img src="docs/screenshots/einstellungen_2026-08-18_proportional.png" alt="Einstellungen-Menü mit Sprachumschaltung" width="480">

*Das Einstellungen-Menü: Sprache, WLAN, Datum/Uhrzeit, Kalender-Adresse und Updates — alles ohne Neuflashen erreichbar.*

Weitere Bildschirmfotos, direkt vom Gerät aufgenommen, liegen unter
[docs/screenshots/](docs/screenshots/).

## Robustheit

Das Gerät läuft unbeaufsichtigt in einer anderen Wohnung. Die Grundregel dabei:
die Anzeige hat Vorrang vor allem anderen — lieber eine ungenaue Uhrzeit als ein
dunkler Bildschirm.

- Ohne WLAN oder Internet läuft die Anzeige mit den zuletzt bekannten Daten
  weiter; der Termin-Cache liegt auf einer eigenen Flash-Partition mit
  Wear-Levelling
- Bis zu fünf WLAN-Netze werden gespeichert; beim Start und nach
  Verbindungsverlust wählt ein Scan automatisch das gerade sichtbare aus
- Hängt beim Start eine Phase, erscheinen Ausweich-Buttons („WLAN wechseln",
  „Offline"); nach 60 Sekunden ohne Reaktion fährt das Gerät selbstständig
  offline fort, mit dem zuletzt angezeigten Zeitstand als Ausgangspunkt
- Ein Task-Watchdog startet das Gerät bei einem hängenden Prozess neu, statt
  eingefroren stehen zu bleiben
- Die Statussymbole rechts oben sind ehrlich: sie erscheinen durchgestrichen,
  wenn die Zeit nur manuell gesetzt wurde oder der Kalender nur aus dem Cache
  kommt

## Konfiguration

Alle Einstellungen sind ohne Neuflashen erreichbar: Das Zahnradsymbol auf dem
Startbildschirm öffnet ein Menü für WLAN (mit Netzwerk-Scan und
Bildschirmtastatur), Datum/Uhrzeit, die Kalender-Adresse und einen Demo-Modus
für Vorführungen ohne WLAN. Sobald das Gerät im WLAN ist, lässt sich die
Kalender-Adresse zusätzlich per Browser ändern — unter `http://seniorenuhr.local`
oder der IP-Adresse des Geräts.

## Hardware

| Komponente | Anmerkung |
|---|---|
| [Waveshare ESP32-S3-Touch-LCD-7](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7) | 7-Zoll-RGB-Display 800 × 480, kapazitiver Touch (GT911) |
| Flash/PSRAM | 8 MB / 8 MB (N8R8) |
| Netzteil | USB-C, mindestens 2 A, Dauerbetrieb |

Der microSD-Slot bleibt ungenutzt; alles Persistente liegt im internen Flash.

## Aufbau der Firmware

Die Firmware basiert auf ESP-IDF 5.5 und LVGL 9 (über esp_lvgl_port). Die Module
liegen als flache C-Dateien in `main/` — je eines für Display-Ansteuerung,
WLAN, Zeit, Kalender-Download, Cache, Anzeige-Logik, Einrichtungsbildschirme und
Web-Konfiguration. Zwei Aufgaben laufen strikt getrennt: die LVGL-Task
aktualisiert die Anzeige im Sekundentakt, eine eigene Kalender-Task erledigt
Download und Parsen — Netzwerkprobleme können die Anzeige dadurch nie ins
Stocken bringen.

Der ICS-Parser ist als portable Komponente (`components/kalender`) geschrieben
und läuft unverändert auch auf dem PC. Er, der Versionsvergleich der
Update-Prüfung und das Tabletten-Langzeitprotokoll werden dort von zusammen
93 Prüfungen abgedeckt (`test_host/`). Alles, was sich ohne Hardware
feststellen lässt, wandert bewusst dorthin: ein Protokoll, das nur beim
Mitternachtswechsel schreibt, wäre auf dem Gerät sonst nur durch Abwarten zu
prüfen. Die Schriften sind selbst generierte LVGL-Fonts auf Basis von
Montserrat, da die in LVGL eingebauten Fonts keine deutschen Umlaute
enthalten.

Firmware-Updates kommen automatisch per OTA von GitHub Releases, werden aber
nie ohne Bestätigung im Einstellungen-Menü installiert — und rollen sich
selbst zurück, falls sich eine neue Version nicht innerhalb weniger Minuten
als funktionsfähig (WLAN und Kalenderabruf) erweist. Die Oberfläche gibt es
auf Deutsch und Englisch, umschaltbar im Einstellungen-Menü; weitere Sprachen
lassen sich bei Bedarf ergänzen.

Die ausführliche Dokumentation liegt im Repository:

- [FAHRPLAN.md](FAHRPLAN.md) — Architektur, Entwurfsentscheidungen und die
  komplette Entwicklungsgeschichte
- [ENTWICKLUNG.md](ENTWICKLUNG.md) — Entwicklungsumgebung und tägliche Befehle
- [FALLSTRICKE_UND_WORKAROUNDS.md](FALLSTRICKE_UND_WORKAROUNDS.md) — über
  vierzig gelöste Probleme mit Ursache und Lösung, vom Flash-Größen-Assert bis
  zu LVGL-Speicherlecks

## Bauen und Flashen

Vorausgesetzt wird [ESP-IDF v5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/get-started/index.html);
gebaut wird mit dessen Werkzeug `idf.py`. Alle Fremdkomponenten (LVGL,
esp_lvgl_port, GT911-Treiber) lädt der ESP-IDF-Komponentenmanager beim ersten
Build automatisch.

```
git clone https://github.com/peterm2024/seniorenuhr.git
cd seniorenuhr
```

Die Zugangsdaten liegen in einer nicht versionierten Datei, die aus der Vorlage
angelegt wird:

```
copy main\secrets.example.h main\secrets.h
```

In `main/secrets.h` WLAN-Name, WLAN-Passwort und die private ICS-Adresse des
Kalenders eintragen. WLAN und Kalender-Adresse lassen sich später auch am
laufenden Gerät ändern.

Dann bauen und flashen — die ESP-IDF-Umgebung muss in der Sitzung aktiviert
sein, der serielle Port (hier `COM3`) steht im Geräte-Manager:

```
idf.py build
idf.py -p COM3 flash
idf.py -p COM3 monitor
```

Der Monitor zeigt das Boot-Log und wird mit `Strg+]` beendet. Bei Problemen
lohnt der Blick in [FALLSTRICKE_UND_WORKAROUNDS.md](FALLSTRICKE_UND_WORKAROUNDS.md),
bevor etwas erneut untersucht wird, das schon einmal gelöst wurde.

## Kein Medizinprodukt, keine Garantie

Dieses Projekt ist ein privates Hobbyprojekt — eine Kalender-Uhr mit
Erinnerungsanzeige. Es ist kein Medizinprodukt im Sinne der
EU-Medizinprodukteverordnung (MDR), hat keine medizinische Zweckbestimmung und
ersetzt weder Pflege noch ärztliche Betreuung noch geprüfte
Medikamenten-Erinnerungssysteme.

Die Anzeige kann jederzeit ausfallen (Stromausfall, WLAN-Störung, Softwarefehler
o. Ä.). Verlasst euch bei kritischen Medikamenten niemals allein auf dieses
Gerät — sichert die Einnahme immer zusätzlich ab, etwa durch Dosierbox, Anruf
oder Pflegedienst.

Die Nutzung erfolgt auf eigene Gefahr. Gewährleistung und Haftung sind im
rechtlich zulässigen Umfang ausgeschlossen (siehe Abschnitte 15–17 der
GPLv3-Lizenz).

## Lizenz

Der Quellcode dieses Projekts steht unter der GNU General Public License v3
(siehe [LICENSE](LICENSE)): Jeder darf ihn frei nutzen, verändern und
weitergeben — wer ihn aber, auch verändert oder in Geräten, weitergibt, muss den
Quellcode unter denselben Bedingungen offenlegen. Copyright © 2026 peterm2024.

Ausnahme: die Schriftdateien in `assets/fonts/` sind aus der Schrift
[Montserrat](https://github.com/JulietaUla/Montserrat) generiert und stehen
unter der SIL Open Font License 1.1
(siehe [assets/fonts/LICENSE-OFL.txt](assets/fonts/LICENSE-OFL.txt)).

Fremdkomponenten (ESP-IDF, LVGL, esp_lvgl_port, GT911-Touchtreiber) sind nicht
Teil dieses Repositories, sondern werden beim Bauen über den
ESP-IDF-Komponentenmanager bezogen; sie stehen unter ihren eigenen permissiven
Lizenzen (Apache 2.0 bzw. MIT).

## English summary

A calendar clock for very old people, built on the Waveshare
ESP32-S3-Touch-LCD-7 (7-inch touch display, 800 × 480). The device sits
permanently in the living room and shows the weekday, time, date, today's
appointments and the medication schedule. Everything is maintained remotely
through an ordinary calendar — nobody has to operate the device itself. It was
built for the author's own parents.

**How it works.** Appointments and medication times live in a normal calendar
that offers a private ICS subscription URL. The clock downloads it every
15 minutes over HTTPS, parses the entries and keeps them in a local cache, so a
loss of internet connectivity does not blank the display.

Medication entries are ordinary recurring calendar events prefixed with
`TABLETTE:` (German) or `PILL:` / `PILLS:` / `MED:` (English). All variants are
recognised **regardless of the selected interface language**, so switching
languages never devalues existing calendar entries. The clock lists these
entries separately and lets them be checked off by touch; the state is stored
locally and survives a restart.

**Design principles.** Large type, high contrast, no scrolling on the main
screen, and a three-stage day/evening/night colour scheme. Deliberate, long
touch gestures rather than small taps, because the intended users have
trembling hands. Nothing blinks except two deliberate exceptions (an
unconfirmed clock time and an overdue medication).

**Reliability.** Showing the time and the medication schedule has absolute
priority — the device continues with the last known time rather than rebooting
into an endless boot loop when Wi-Fi is unavailable. Firmware updates are
delivered over the air from GitHub releases, are never installed without
confirmation, and roll back automatically if the new version cannot prove
itself (Wi-Fi plus calendar) after installation.

**Languages.** The interface ships in German and English. Further languages can
be added by extending one table in `main/texte.c`; note that anything beyond
Latin-1 also requires regenerating the fonts
(`tools/fonts/erzeuge_fonts.ps1`). Documentation, source comments and log
output remain in German — the log output deliberately so, as it is the
project's debugging tool.

**Hardware.** Waveshare ESP32-S3-Touch-LCD-7, the N8R8 variant: 8 MB flash (not
16 MB as the product page suggests) and 8 MB octal PSRAM, with a GT911 touch
controller. There is no battery-backed real-time clock; the time is fetched via
NTP.

**Building.** ESP-IDF 5.5. Copy `main/secrets.example.h` to `main/secrets.h`,
fill in the Wi-Fi credentials and the calendar URL, then `idf.py build` and
`idf.py -p <PORT> flash`. See the German section
[Bauen und Flashen](#bauen-und-flashen) for details.

**Not a medical device, no warranty.** This is a private hobby project — a
calendar clock with a reminder display. It is not a medical device under the EU
Medical Device Regulation, has no medical purpose, and replaces neither care
nor medical supervision nor certified medication reminder systems. The display
can fail at any time (power cut, Wi-Fi outage, software fault). Never rely on
this device alone for critical medication — always add a second safeguard such
as a pill organiser, a phone call or a care service. Use at your own risk;
warranty and liability are excluded to the extent permitted by law (see
sections 15–17 of the GPLv3).

**Licence.** Source code under the GNU General Public License v3 (see
[LICENSE](LICENSE)), Copyright © 2026 peterm2024. Exception: the font files in
`assets/fonts/` are generated from
[Montserrat](https://github.com/JulietaUla/Montserrat) and are covered by the
SIL Open Font License 1.1.
