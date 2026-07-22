# Seniorenuhr — Fahrplan & Architektur

Eine Kalender-Uhr für hochbetagte Menschen auf Basis des **Waveshare ESP32-S3-Touch-LCD-7**.
Zeigt dauerhaft: Uhrzeit, Wochentag, Datum, Tageszeit („Vormittag/Nachmittag/Abend") sowie die
**heutigen Termine und Tabletten**. Die Pflege der Daten geschieht aus der Ferne (z. B. per Handy),
die Eltern müssen nichts bedienen.

---

## 1. Die Hardware (was wir haben)

| Komponente | Detail | Bedeutung für uns |
|---|---|---|
| ESP32-S3 Dual-Core, 240 MHz | 512 KB SRAM, **8 MB Flash, 8 MB PSRAM** (Produktseite nennt 16 MB — verbaut sind 8, per flash_id geprüft) | Genug Leistung für flüssige Grafik (LVGL) |
| 7"-LCD, 800×480, 65K Farben | RGB-Parallel-Interface | Groß genug für sehr große Schrift |
| Kapazitiver Touch (GT911) | 5-Punkt, per I2C | Optional: „Tablette genommen"-Bestätigung |
| WLAN 2,4 GHz + Bluetooth 5 LE | Onboard-Antenne | Uhrzeit (NTP) + Kalender-Abruf + Fernwartung |
| microSD-Slot | *(bisher ungenutzt)* | Cache liegt stattdessen auf einer eigenen Flash-Partition (FAT + Wear-Levelling, siehe unten) |
| I2C-/UART-Header | | Später erweiterbar (z. B. DS3231-RTC, Helligkeitssensor) |
| USB-C | Programmierung + Strom | Dauerbetrieb am USB-Netzteil |

**Keine batteriegepufferte RTC an Bord** → nach Stromausfall kommt die Uhrzeit per WLAN/NTP zurück.
Solange kein WLAN da ist, zeigt die Uhr „Uhrzeit wird geholt…" statt einer falschen Zeit.

**Zusätzlich benötigt/empfohlen:**
- Stabiles USB-C-Netzteil (≥ 2 A) + ausreichend langes Kabel
- microSD-Karte (klein, z. B. 8–32 GB)
- Gehäuse/Aufsteller (3D-Druck, Bilderrahmen-Umbau oder Waveshare-Case)
- **WLAN bei den Eltern** — zentrale Voraussetzung für Fernpflege der Termine
- Optional: DS3231-RTC-Modul (~3 €) als Uhrzeit-Fallback, falls WLAN unzuverlässig ist

---

## 2. Architektur

### 2.1 Grundprinzip

```
   Peter (Handy/PC)                    Wohnung der Eltern
┌────────────────────┐             ┌──────────────────────────┐
│ Kalender-App       │             │  Seniorenuhr (ESP32-S3)  │
│ (Google/Nextcloud) │             │                          │
│                    │   HTTPS     │  ┌────────┐  ┌─────────┐ │
│  Termine +         │──(ICS-URL)──┼─▶│ Sync-  │─▶│ SD/Flash│ │
│  Tablettenplan     │             │  │ Dienst │  │ (Cache) │ │
└────────────────────┘             │  └────────┘  └────┬────┘ │
                                   │       NTP ──▶ Uhrzeit    │
                                   │                   ▼      │
                                   │            ┌──────────┐  │
                                   │            │ LVGL-UI  │  │
                                   │            │ 800×480  │  │
                                   │            └──────────┘  │
                                   └──────────────────────────┘
```

- **Peter pflegt Termine und Tablettenzeiten in einem normalen Kalender** (empfohlen: Google
  Kalender oder Nextcloud — beide liefern eine geheime ICS-Abo-URL).
- Die Uhr lädt diesen Kalender z. B. alle 15 Minuten per HTTPS, parst die Einträge und
  **cached sie lokal** — fällt das Internet aus, zeigt sie weiter die letzten bekannten Daten.
- Tabletten werden als wiederkehrende Kalendereinträge mit Präfix gepflegt, z. B.
  `TABLETTE: Blutdruck (1x morgens)` — die Uhr erkennt das Präfix und zeigt sie mit
  Pillen-Symbol in einer eigenen Liste.
- Die Eltern müssen **nichts** bedienen. Touch ist ein optionales Extra (z. B. „✓ genommen").

### 2.2 Software-Stack

| Schicht | Wahl | Begründung |
|---|---|---|
| Framework | **ESP-IDF 5.5** (VS Code-Extension) | Waveshare-Beispiele nutzen es; RGB-Display + PSRAM sauber konfigurierbar |
| Grafik | **LVGL 9** (via esp_lvgl_port) | Standard für dieses Board |
| UI-Schriften | Eigene LVGL-Fonts (lv_font_conv, Montserrat-Bold) | Die eingebauten Fonts haben **keine deutschen Umlaute** — eigene Fonts (28/40/72/128 px) mit ä/ö/ü/ß, siehe `assets/fonts/` |
| Uhrzeit | SNTP + Zeitzone `Europe/Berlin` | Sommer-/Winterzeit automatisch (`CET-1CEST,M3.5.0,M10.5.0/3`) |
| Termine | ICS-Abruf per HTTPS (esp_http_client) + eigener Mini-Parser | Nur „heute" nötig → Parser bleibt klein, läuft identisch auf PC und ESP32 (`components/kalender`) |
| Speicher | NVS (WLAN-Zugang), FAT+Wear-Levelling auf eigener Flash-Partition „speicher" (Termin-Cache) | Übersteht Neustarts und Internetausfälle, schont die Flash-Zellen bei häufigem Schreiben |
| Wartung | WLAN-Watchdog (Neustart nach 30s ohne Verbindung) fertig; OTA-Update **noch offen** (Phase 5) | Erste Robustheits-Stufe steht, Fernwartung folgt |

### 2.3 Module der Firmware (tatsächlicher Stand)

```
seniorenuhr/
├── main/
│   ├── app_main.c          – Start: Boot-Phasen, UI-Aufbau, Sekunden-Tick, Status-Symbole
│   ├── anzeige.c/.h         – CH422G, RGB-Panel-Init, GT911-Touch, esp_lvgl_port
│   ├── startbildschirm.c/.h – Boot-Anzeige (3 Symbole + Countdown-Ring + Recovery-Buttons)
│   ├── einrichtung.c/.h     – Einrichtungsbildschirme "WLAN wechseln"/"Offline-Zeit setzen"
│   ├── tagesansicht.c/.h    – Wochentag-Navigation + Tabletten-Abhaken (Tages-/Heute-Fenster)
│   ├── netz.c/.h            – WLAN-Verbindung, mehrere gemerkte Netze (NVS), Reconnect, Watchdog
│   ├── zeit.c/.h            – SNTP, Zeitzone, Wochentag/Tageszeit-Text, manuelles Setzen
│   ├── kalender_holen.c/.h    – HTTPS-Download des ICS-Kalenders
│   ├── kalender_speicher.c/.h – Cache auf eigener Flash-Partition
│   ├── kalender_anzeige.c/.h  – Hintergrund-Task: verbindet Abruf+Cache+Parser, Tages-Status
│   └── secrets.h (gitignored) – WLAN-Zugang + Kalender-URL
├── components/kalender/     – ICS-Parser (portables C, auch fuer PC-Tests)
├── assets/fonts/             – generierte LVGL-Fonts
├── test_host/                 – PC-Tests fuer den Parser
└── FAHRPLAN.md
```

Statt der ursprünglich geplanten Unterordner je Bereich sind die Module als flache Dateien
in `main/` organisiert — bei der aktuellen Größe übersichtlich genug, eine Aufteilung in
Unterordner kann bei Bedarf später erfolgen.

Zwei Kernaufgaben laufen getrennt: die **LVGL-Task** (Anzeige, jede Sekunde Uhr aktualisieren)
und die **Kalender-Task** (WLAN-Abruf alle 15 Min) — so ruckelt die Anzeige nie, egal was das
Netz macht. Ein `esp_timer`-Watchdog startet das Gerät automatisch neu, wenn länger als 30s
keine WLAN-Verbindung besteht.

### 2.4 Bildschirm-Entwurf (seniorengerecht)

```
┌──────────────────────────────────────────────┐
│              DONNERSTAG                      │  ← sehr groß, volle Breite
│           10:42 Uhr                          │  ← riesig (~140 pt)
│        Vormittag · 10. Juli 2026             │
├──────────────────────────────────────────────┤
│  💊 TABLETTEN HEUTE                          │
│     Morgens   – Blutdruck  ✓                 │
│     Abends    – Herz                         │
├──────────────────────────────────────────────┤
│  📅 TERMINE HEUTE                            │
│     15:00  Dr. Müller (Hausarzt)             │
│     — morgen: Friseur 10:00 —                │
└──────────────────────────────────────────────┘
```

Gestaltungsregeln (bewährt bei Demenz-/Seniorenuhren):
- **Wochentag ausgeschrieben und zuoberst** — die häufigste Frage ist „Welcher Tag ist heute?"
- **Tageszeit in Worten** („Vormittag/Nachmittag/Abend/Nacht") — 07:00 vs. 19:00 ist auf
  Digitaluhren leicht zu verwechseln
- Sehr hoher Kontrast (weiß auf dunkelblau/schwarz), keine Animationen, nichts blinkt
- Nachts automatisch stark gedimmt, dunkles Layout (Schlafzimmer-tauglich)
- Anstehende Tablette zur Einnahmezeit **farblich hervorheben** (statt Alarm-Ton)
- Maximal die 3–4 nächsten Einträge — keine überfüllten Listen

**Tatsächlich umgesetzt (Stand: 13.07.2026):** Layout wie oben (ohne Emoji — die eigenen
Fonts enthalten nur Buchstaben/Umlaute), plus drei Anzeigemodi statt einer reinen Dimmung:

- **Tag:** volle Farben (dunkelblauer Hintergrund, gelber Wochentag, weiße Uhrzeit)
- **Abend** (18–21:59 Uhr): Bildschirm gleichmäßig abgedunkelt, Tabletten/Termine bleiben sichtbar
- **Nacht** (22–5:59 Uhr): Hintergrund komplett schwarz, alle Schrift nur noch dunkelgrau,
  Tabletten/Termine werden ausgeblendet (nachts nicht relevant, spart zusätzlich Licht)
- **Berührung während Abend/Nacht** schaltet für 30 Sekunden auf volle Tag-Helligkeit
  (inklusive Tabletten/Termine), danach automatisch zurück
- Der **Bootvorgang** zeigt einen eigenen Startbildschirm (drei Symbole: WLAN, Uhr, Kalender,
  die nacheinander blinken und weiß werden, mit 4px-Countdown-Ring pro Phase), danach blendet
  die Hauptanzeige über 2 Sekunden passend zur Tageszeit ein, statt hart zu erscheinen
- **Recovery beim Boot:** Hängt eine Phase (WLAN/Uhr/Kalender) länger als 30s, erscheinen zwei
  Buttons „WLAN wechseln" (Zugangsdaten-Eingabe mit Bildschirmtastatur, landet im NVS) und
  „Offline" (Datum/Uhrzeit manuell setzen, ohne NTP weiterfahren). Läuft die Phase 60s ohne
  Reaktion, startet das Gerät neu.
- **Mehrere WLAN-Netze:** Das Gerät merkt sich bis zu 5 Netze (leicht verschleiert im NVS) und
  wählt beim Start per Scan automatisch das gerade sichtbare aus — praktisch beim Wechsel
  zwischen Testaufbau zu Hause und dem Einsatzort.
- **Live-Status-Symbole** rechts oben (WLAN/Zeit/Kalender, kleine Ringe mit Mini-Glyphen)
  zeigen auf einen Blick, ob alles wirklich aktuell ist — durchgestrichen bei fehlender
  Konnektivität bzw. bei nur manuell gesetzter Zeit/gecachten (nie frisch heruntergeladenen)
  Kalenderdaten.
- **Wochentag-Navigation:** 7 Buttons links (gestern..+5 Tage), Position 2 ist ein etwas
  breiterer „Heute"-Button. Wochentag-Buttons öffnen ein 15s lang eingeblendetes Tages-Fenster
  mit Terminen/Tabletten in zwei Spalten (wie der Hauptbildschirm), auf 5 Zeilen pro Spalte
  begrenzt („+N weitere" statt Überlauf); vergangene Termine erscheinen grau/durchgestrichen.
  Der „Heute"-Button öffnet ein eigenes Fenster mit einem breiten Schiebeschalter pro Tablette
  zum Abhaken (bleibt bis Mitternacht bestehen). Beide Fenster haben einen „X"-Button zum
  manuellen Schließen; der Button, dessen Fenster gerade offen ist, bekommt einen hellblauen
  Rahmen. Wochentag-Beschriftungen färben sich nach Terminanzahl des Tages (1×hellorange,
  2×dunkelorange, 3+×rot). Die Tabletten-Übersicht auf dem Hauptbildschirm zeigt abgehakte
  Tabletten ebenfalls gedämpft an.

---

## 3. Fahrplan (Phasen)

Jede Phase endet mit etwas Sichtbarem, das funktioniert. **Tatsächliche Reihenfolge wich vom
Plan ab:** Phase 4 (Kalender) wurde vor Phase 2/3 vorgezogen, da Peter das früh sehen wollte;
Phase 2 (Layout/Tag-Nacht) kam danach und deckt inhaltlich auch das ab, was für Phase 3
(Testdaten-Anzeige) geplant war — echte Kalenderdaten waren zu dem Zeitpunkt schon da, ein
Umweg über Testdaten war nicht mehr nötig.

### Phase 0 — Werkzeugkasten & Hardware-Test *(½ Tag)* ✅ ERLEDIGT (13.07.2026)
- ESP-IDF 5.5 + VS Code-Extension installiert, hello_world erfolgreich geflasht
- Board als N8R8-Variante identifiziert (8 MB Flash statt beworbener 16 MB, siehe
  FALLSTRICKE_UND_WORKAROUNDS.md #2)

### Phase 1 — Die nackte Uhr *(1–2 Tage)* ✅ ERLEDIGT (13.07.2026)
- WLAN-Verbindung + SNTP, Zeitzone Europe/Berlin, automatischer Reconnect
- Anzeige: Uhrzeit, Wochentag, Datum (deutsch), Sekundentakt-Update
- Getestet: zeigte korrekt „Montag 21:03 13. Juli 2026"

### Phase 4 — Kalender-Anbindung ✅ ERLEDIGT (13.07.2026, vorgezogen)
- HTTPS-Download (esp_http_client + TLS-Zertifikatsbündel) des privaten Google-Kalender-ICS
- Eigener ICS-Parser (`components/kalender`) — läuft identisch auf PC (25 Tests) und ESP32
- Cache auf eigener Flash-Partition (FAT + Wear-Levelling statt SD-Karte)
- Hintergrund-Task: Abruf alle 15 Min, Retry nach 30s bei Fehlern, erkennt Mitternachtswechsel
- Getestet gegen den echten Familienkalender: korrekte Tabletten/Termine für den jeweiligen Tag

### Phase 2 — Seniorengerechte Anzeige ✅ ERLEDIGT (13.07.2026)
- Eigene Fonts mit Umlauten (28/40/72/128 px, Montserrat-Bold)
- Tag/Abend/Nacht-Farbschema (siehe Abschnitt 2.4) statt einer reinen Dimmung
- Berührung weckt für 30s in den Tag-Modus
- Startbildschirm mit drei Symbolen (WLAN/Uhr/Kalender), sanftes Einblenden der Hauptanzeige

### Phase 3 — Wochentag-Navigation & Tabletten-Abhaken ✅ ERLEDIGT (15.07.2026)
- 7 Wochentag-Buttons links öffnen ein Tages-Fenster (Termine/Tabletten in zwei Spalten, 15s
  bzw. per „X" manuell schließbar) für gestern..+5 Tage
- „Heute"-Button (Position 2, etwas breiter) öffnet ein Fenster mit Schiebeschalter pro
  Tablette zum Abhaken, bestätigter Status bleibt bis Mitternacht bestehen (auch über
  Kalender-Refreshes hinweg)
- kalender_anzeige.c liefert dafür strukturierte Tageseinträge statt nur fertig formatierter
  Anzeige-Texte
- Nachtrag nach weiterem Feinschliff (gleicher Tag): aktiver Rahmen um den gerade offenen
  Button, Terminanzahl-Farbcodierung, vergangene Termine grau/durchgestrichen, abgehakte
  Tabletten auch in der Hauptbildschirm-Übersicht gedämpft, Schiebeschalter rechtsbündig per
  `lv_obj_align` (siehe FALLSTRICKE_UND_WORKAROUNDS.md #11)

### Phase 5 — Robustheit & Fernwartung *(teilweise begonnen, 15.07.2026)*
- ✅ WLAN-Watchdog: Neustart nach 30s ununterbrochen ohne Verbindung (pausiert waehrend der
  Einrichtungsbildschirme, damit er nicht mitten in die Eingabe hinein feuert)
- ✅ Recovery-Bildschirme "WLAN wechseln"/"Offline" nach 30s hängender Boot-Phase
- ✅ Mehrere WLAN-Netze gemerkt (NVS, leicht verschleiert), automatische Auswahl per Scan
- ✅ Live-Status-Symbole (WLAN/Zeit/Kalender) rechts oben, ehrlich durchgestrichen bei
  unbestätigten Daten (manuelle Zeit, nur gecachter statt frisch heruntergeladener Kalender)
- ✅ Einstellungen-Menü (15.07.2026): Zahnrad-Symbol unten rechts auf dem Startbildschirm
  (von Anfang an sichtbar, nicht erst nach 30s) öffnet ein Menü mit sofortigem Zugriff auf
  "WLAN wechseln"/"Datum, Uhrzeit einstellen" sowie neu: Schalter für die künftige
  Buzzer-Tonausgabe (siehe "Später/Ideen") und ein Textfeld für die Kalender-Adresse
  (überschreibt/löst secrets.h zur Laufzeit ab, persistiert im NVS). Neues Modul
  main/einstellungen.c/h dafür. Die manuelle Datum/Uhrzeit-Eingabe geht jetzt vom zuletzt
  angezeigten Zeitstempel aus statt von der rohen Systemzeit (nach Stromausfall sonst 1970).
  Versuch, dabei auch eine 180°-Display-Rotation (Kabelaustritt oben) einzubauen, wurde nach
  einem gefundenen Flacker-Bug (direct_mode/Anti-Tearing vertragen sich nicht mit LVGLs
  Rotationswegen) wieder verworfen — Kabelführung wird stattdessen hardwareseitig gelöst,
  siehe FALLSTRICKE_UND_WORKAROUNDS.md #12.
- ✅ Nachtrag nach erstem Demo-Einsatz (17.07.2026): Reset-Grund wird beim Boot geloggt
  (`esp_reset_reason()`, siehe FALLSTRICKE #13 zum Anlass); Anzeige bleibt jetzt im Tag-Modus,
  solange ein Tages-/Heute-Fenster offen ist (vorher fiel sie mitten in der Tabletten-Bedienung
  in den Nacht-Modus zurück, siehe FALLSTRICKE #13); Tabletten/Termine-Übersicht auf dem
  Hauptbildschirm ist jetzt antippbar und öffnet direkt das Heute-Fenster; Einstellungen-Menü
  überarbeitet (nur noch Buttons/Schalter mit LV_SIZE_CONTENT statt geratener Festbreite, die
  Buttontexte abgeschnitten hatte) und die Kalender-Adresse hat jetzt einen eigenen,
  mehrzeiligen Bildschirm statt eines zu schmalen einzeiligen Textfelds im Menü selbst.
- ✅ Nachtrag 2 (17.07.2026): abgehakte Tabletten bekommen zusätzlich ein "[x] "-Präfix
  (Hauptbildschirm-Übersicht, Tages-Fenster, Heute-Fenster — dort live beim Loslassen des
  Schiebereglers aktualisiert), vergangene Termine sind jetzt auch auf dem Hauptbildschirm
  durchgestrichen (vorher nur gedämpfte Farbe). Dafür wurde die Hauptbildschirm-Übersicht von
  einem einzigen mehrzeiligen Recolor-Label auf ein Label pro Eintrag umgebaut, da
  Durchstreichen nur pro Objekt setzbar ist. Kein Unicode-Häkchen (Font enthält keine
  Symbolglyphen, siehe tools/fonts/erzeuge_fonts.ps1) — bewusst ASCII "[x] " statt dessen.
  WLAN-Signalstärke ist jetzt im Einstellungen-Menü ablesbar (`netz_rssi_dbm()`) — Anlass war
  Peters Vermutung, ein schwaches Signal an seinem Platz könnte Ursache für einen unerklärten
  Neustart und eine kurzzeitig fehlende WLAN-Verbindung sein; per Boot-Log bestätigt: RSSI lag
  bei -77 dBm ("schwach"), ein WLAN-Verbindungsversuch schlug einmal fehl, bevor der zweite
  gelang. Ausserdem: Reset-Grund wird jetzt sofort beim Boot geloggt (siehe Nachtrag 1).
- ✅ Nachtrag 3 (17.07.2026) — Neustart-Schleife bei schwachem/fehlendem WLAN ausgeschlossen:
  Peter wies zurecht darauf hin, dass ein Neustart bei WLAN-Verlust riskant ist, wenn dadurch
  eine faellige Tablette nicht angezeigt wird (Prioritaet: Anzeige > korrekte Uhrzeit). Zwei
  Aenderungen: (1) der WLAN-Watchdog (30s → Neustart) wird jetzt erst NACH dem ersten
  Erreichen der Hauptanzeige auf 1 Woche gelockert (`netz_watchdog_lockern()`); (2) der
  60s-Boot-Timeout je Phase fuehrt nicht mehr zu einem Neustart, sondern macht automatisch
  weiter wie beim manuellen "Offline"-Button (`phase_timeout_automatisch_fortsetzen()`) - die
  Uhrzeit wird dabei auf den zuletzt angezeigten Stand gesetzt (`zeit_uebernehmen()`) und die
  grosse Uhrzeit blinkt dunkelorange, solange sie unbestaetigt ist. Am Geraet verifiziert
  (testweise falsche WLAN-SSID, kompletten 60s-Timeout abgewartet): kein Neustart, Hauptanzeige
  erscheint trotzdem. Details siehe FALLSTRICKE_UND_WORKAROUNDS.md #14.
- ✅ Nachtrag 4 (17.07.2026): der bei Boot-Timeout uebernommene "letzte bekannte Zeitstand"
  beruecksichtigt jetzt zusaetzlich die seit dem Einschalten bereits verstrichene Zeit
  (`esp_timer_get_time()`), statt einfach den alten Zeitstempel unveraendert zu uebernehmen.
  Ausserdem: der Tabletten-Bestaetigungsstatus (abgehakt/nicht abgehakt) wird jetzt auf der
  Kalender-Cache-Partition persistiert (`kalender_speicher_bestaetigungen_schreiben/_lesen`) und
  ueberlebt damit einen unerwarteten Neustart mitten am Tag - ein Ruecksync ins ICS/Google-
  Calendar wurde verworfen, da die genutzte private ICS-Adresse rein lesend ist und ein
  Zurueckschreiben die volle Calendar-API samt OAuth2 braeuchte. Erster Live-Test zeigte einen
  echten Bug (Dateiname zu lang fuer 8.3-Kurznamen bei `CONFIG_FATFS_LFN_NONE`, `fopen` scheiterte
  mit EINVAL) - nach Umbenennung erneut getestet: Peter bestaetigte, der Haken bleibt jetzt nach
  einem Neustart erhalten. Details siehe FALLSTRICKE_UND_WORKAROUNDS.md #15.
- ✅ Nachtrag 5 (18.07.2026) — WLAN-Dropdown, Signal-Balken und ein hartnaeckiger Einfrier-Bug:
  Zwei Komfort-Wuensche umgesetzt: der WLAN-Bildschirm zeigt gefundene Netzwerke jetzt als
  Dropdown-Liste (Scan startet automatisch beim Oeffnen, manuelle Eingabe bleibt als Fallback
  fuer versteckte Netze), und die WLAN-Signalstaerke im Einstellungen-Menue hat einen live
  aktualisierten, farbigen Balken. Dabei traten wiederholt komplette Geraete-Einfrierungen auf,
  deren Ursachenjagd sich ueber mehrere falsche Faehrten zog (kaputte Nachbar-SSIDs, Stack-
  Overflow der WiFi-Event-Task, Use-after-free im LVGL-Dropdown - alle drei real und behoben,
  aber keine war der Haenger) und erst per Task-Watchdog-Panic + Core-Dump ueber UART geloest
  wurde: das Einstellungen-Menue leakte bei jeder Unter-Navigation einen kompletten Screen in
  den nur 64 KB grossen LVGL-Pool; war der voll, blieb die Zeichen-Pipeline endlos haengen.
  Details siehe FALLSTRICKE_UND_WORKAROUNDS.md #16. Als dauerhaftes Sicherheitsnetz startet das
  Geraet bei einem haengenden Task jetzt nach 5s automatisch neu (statt eingefroren zu bleiben -
  Anzeige hat Prioritaet) und schreibt dabei einen Core-Dump ins serielle Log
  (sdkconfig.defaults: CONFIG_ESP_TASK_WDT_PANIC + CONFIG_ESP_COREDUMP_ENABLE_TO_UART).
  Nach dem Fix: 10+ intensive Menue-Runden ohne ein einziges Watchdog-Ereignis, von Peter
  bestaetigt stabil.
- ✅ Nachtrag 6 (19.07.2026) — Demo-Modus fuer Vorfuehrungen ohne WLAN: neuer Button im
  Einstellungen-Menue (Zahnrad ist ab der ersten Sekunde erreichbar), setzt die Uhrzeit auf den
  festen Demo-Zeitstempel 18.07.2026 17:59 und ueberspringt alle noch offenen Boot-Phasen -
  direkt zur Hauptanzeige statt 60s-Countdowns abzuwarten (Anlass: Biergarten-Vorfuehrung an
  der Powerbank). Die Demo-Zeit wird bewusst NICHT als "zuletzt angezeigt" persistiert, damit
  der Boot-Fallback zuhause nie mit dem Fantasie-Datum startet.
- ✅ Nachtrag 7 (19.07.2026) — WLAN-Scan findet iPhone-Hotspots zuverlaessig: der WLAN-Bildschirm
  scannte bisher genau EINMAL beim Oeffnen; iPhone-Hotspots kuendigen sich im Leerlauf aber nur
  sparsam an und fehlten deshalb oft dauerhaft in der Liste ("Peters iPhone" stand laut PC-Scan
  auf Kanal 6 mit 95% Signal und wurde trotzdem meist verpasst). Jetzt: Dauerscan alle ~2s
  solange der Bildschirm offen ist (Ergebnisse werden vereinigt, einmal gesehen bleibt),
  laengere Verweildauer pro Kanal (300ms statt 120ms), Dropdown-Optionen nur bei tatsaechlicher
  Aenderung angefasst. Live verifiziert: Hotspot in jeder Scan-Runde gefunden, Speichern +
  Verbinden klappt (Boot-Log: "connected with Peters iPhone").
- ✅ Nachtrag 8 (19.07.2026) — WLAN-Neuscan im Laufbetrieb: der Reconnect nach einem Abbruch
  probierte bisher stur immer nur das zuletzt verbundene Netz; die Scan-basierte Profilauswahl
  lief nur beim Boot. Folge (live beobachtet): nach einem Hotspot-Test blieb das Geraet dauerhaft
  offline, obwohl das Heimnetz sichtbar war - erst Stromziehen half. Jetzt: bleibt die Verbindung
  im Laufbetrieb >60s weg, laeuft die Boot-Auswahl (bestes sichtbares bekanntes Netz) in einer
  eigenen kleinen Task erneut, alle 60s bis zum Erfolg. Gilt auch nach einem komplett offline
  durchgelaufenen Boot (unterwegs gestartet, dann heimgekommen). Waehrend des Bootens bewusst
  unveraendert (60s-Countdown/30s-Watchdog, keine Neustart-Schleifen-Gefahr). Live verifiziert:
  Hotspot aus -> nach 63s Neuscan -> ohne Neustart zurueck ins Heimnetz (68s Gesamtdauer).
  Ausserdem behoben: wirre Zeichen ("'_?0 Frueh") nach dem Zurueckschieben des Tabletten-
  Schiebers - lv_label_set_text bekam einen Zeiger in den eigenen Label-Puffer (siehe
  FALLSTRICKE #17).
- ✅ Nachtrag 9 (19.07.2026) — Kalender-Adresse per Browser aendern (Web-Konfigurationsseite):
  bisherige Wege waren entweder ein USB-Kabel zum Computer oder muehsames Abtippen der langen,
  kryptischen Google-Calendar-URL auf dem Touchscreen. Neu: sobald WLAN besteht, laeuft ein
  kleiner eingebauter Webserver (esp_http_server) mit einer einzigen Seite - "/" zeigt ein
  Formular mit der aktuellen Kalender-Adresse, "/speichern" persistiert eine neue (leer =
  zurueck auf secrets.h-Standard). Erreichbar per IP-Adresse oder ueber den registrierten
  mDNS-Namen "seniorenuhr.local" (funktioniert von Handys/Mac i.d.R. sofort, Windows braucht
  dafuer zusaetzlich Bonjour - dort einfach die IP-Adresse nutzen). Dabei ein Stack-Overflow im
  httpd-Handler gefunden und behoben (siehe FALLSTRICKE #18). Ausserdem beim Testen bemerkt:
  das Geraet lief noch auf einem versehentlich eingerichteten isolierten Gaeste-WLAN
  ("Heimnetz G") - auf das normale Hauptnetz umgezogen, seitdem im selben Subnetz wie alle
  anderen Heimgeraete erreichbar. Live per curl verifiziert: GET zeigt aktuelle URL, POST
  speichert neue Adresse und faellt bei leerem Wert korrekt auf den secrets.h-Standard zurueck.
- ✅ Nachtrag 10 (21.07.2026) — Bildschirmfoto-Entwicklungswerkzeug (nur fuer die Doku, kein
  Laufzeit-Feature): ein Button unten mittig (liegt per lv_layer_top() automatisch ueber JEDEM
  Bildschirm) nimmt ein Foto der gerade aktiven Anzeige auf und gibt es als Base64-BMP ueber die
  serielle USB-Verbindung aus (Empfang/Dekodierung per kleinem Python-Skript). Ein erster Anlauf
  ueber den Web-Konfigurationsserver (HTTP) scheiterte an extrem langsamer WLAN-Uebertragung
  (~1,8 KB/s, vermutlich PSRAM/WLAN-Bus-Konkurrenz) und wurde komplett verworfen - da das Feature
  ohnehin nur waehrend der Entwicklung gebraucht wird, war die serielle Verbindung der einfachere
  und robustere Weg. Dabei zwei Task-Watchdog-Neustarts und ein "Geisterbild" durch nicht
  genullten Puffer gefunden und behoben (siehe FALLSTRICKE #19). Danach noch ein vierter Fehler
  gefunden (per Quellcode-Vergleich zweifelsfrei bestaetigt, nicht nur "so gesehen"): einige
  Wochentag-Buttons erschienen am falschen (rechten statt linken) Bildschirmrand, weil LVGLs
  eingebaute Snapshot-Funktion eine fuer echte Bildschirm-Refreshs gedachte Teil-Redraw-
  Optimierung nutzt, die bei einem frischen, leeren Puffer nicht passt - durch eine eigene,
  immer vollstaendig neu zeichnende Ersatzfunktion behoben (FALLSTRICKE #19, Fallstrick D). Alle
  sieben Wochentag-Buttons stehen jetzt nachweislich korrekt in einer Spalte am linken Rand.
  Vor dem Einzug bei den Eltern wieder aus app_main.c entfernen.
- ✅ Nachtrag 11 (21.07.2026) — Kalender-Sync dauerhaft tot (durchgestrichenes Symbol auf dem
  Doku-Screenshot bemerkt): Kalender-Downloads scheiterten seit einigen Features (Web-Konfig,
  mDNS, WLAN-Neuscan-Task, Screenshot-Werkzeug) ausnahmslos, live im Log als
  `mbedtls_ssl_setup returned -0x7F00` (Speicher-Allokationsfehler) erkannt. Ursache: mbedTLS
  war per `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC` (ESP-IDF-Standard) darauf festgelegt, seine
  ueber 20 KB grossen SSL-Puffer zwingend aus dem knappen internen SRAM (~200 KB) statt aus dem
  fast ungenutzten 8-MB-PSRAM zu holen - die zusaetzlichen Tasks/Stacks der neueren Features
  liessen dafuer keinen Platz mehr. Behoben durch `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`
  (sdkconfig.defaults). Live verifiziert: Download klappt seitdem zuverlaessig (siehe
  FALLSTRICKE #20).
- ✅ Nachtrag 12 (21.07.2026) — Zweites Board in Betrieb genommen und Screenshot-Werkzeug
  gehaertet: Das baugleiche zweite ESP32-S3-Board flasht/bootet identisch (Display, Touch, WLAN,
  Kalender-Sync alle in Ordnung). Beim Testen fiel ein reiner Aufnahme-Fehler auf: ein Boot-
  Screenshot des Startbildschirms zeigte ALLES um 121px nach links verschoben (auf dem echten
  Display korrekt, nur im Bild). Ursache: die Snapshot-Funktion verankerte den Render-Puffer hart
  bei (0,0) statt an den echten Screen-Koordinaten - behoben (FALLSTRICKE #19, Fallstrick E).
  Zusaetzlich Peters Idee einer Versatz-Selbstkontrolle umgesetzt: beim Aufnehmen wird eine
  schmale Magenta-Referenzlinie an eine feste Spalte mitgerendert; das Dekodier-Skript prueft ihre
  Ankunftsposition, meldet/korrigiert einen etwaigen Versatz (Render ODER serielle Uebertragung)
  und rechnet die Marke danach wieder heraus. End-to-End live verifiziert (Marke bei x=10, kein
  Versatz, im fertigen Bild rueckstandslos entfernt). Doku-Screenshots des Startbildschirms
  ergaenzt (docs/screenshots/).
- ✅ Nachtrag 13 (21.07.2026) — Rollenteilung der beiden Boards + Produktions-Build-Schalter:
  Board 1 (an COM3) bleibt das Entwicklungsboard, Board 2 (an COM5) geht zum Testen an Peters
  Eltern. Damit Entwicklungswerkzeuge (aktuell nur der Screenshot-Button) nicht auf dem
  Eltern-Geraet landen, gibt es jetzt den Compile-Schalter `ENTWICKLUNGSWERKZEUGE` in app_main.c:
  Standard 1 (Entwicklungsboard, Screenshot-Button aktiv), fuer einen Produktions-Build auf 0
  setzen und neu bauen - dann faellt der Button weg und der ungenutzte screenshot_debug-Code wird
  vom Linker ganz aus dem Binary geworfen. Der Demo-Modus im Einstellungsmenue ist bewusst NICHT
  betroffen (bleibt auch auf dem Eltern-Geraet verfuegbar, Peters Wunsch). Board 1 mit dem vollen
  Dev-Stand geflasht, Board 2 mit dem Produktions-Build (Screenshot-Button per Boot-Log als
  entfernt bestaetigt). Damit beginnt faktisch Phase 6 (erstes Test-Geraet bei den Eltern).
- ⬜ OTA-Updates, sauberer Kaltstart-Test nach echtem Stromausfall
- ⬜ Beobachtet, aber noch nicht behoben (unkritischer Rest nach Nachtrag 11): gelegentliche
  einzelne Kalender-Downloads scheitern weiterhin bei schwachem WLAN-Signal
  (`MBEDTLS_ERR_SSL_CONN_EOF` waehrend des TLS-Handshakes) - heilt sich ueber die bestehende
  30s-Wiederholung zuverlaessig von selbst, durch Nachtrag 3 ohnehin nicht mehr
  neustart-kritisch, da die Anzeige davon unabhaengig laeuft
- **Ziel:** Eine Woche Dauerlauf bei den Eltern ohne Eingriff

### Phase 6 — Einzug bei den Eltern *(1 Tag + Beobachtung)*
- Gehäuse/Aufsteller, Kabelführung, Platzwahl (Augenhöhe, kein Gegenlicht, Steckdose)
- WLAN der Eltern eintragen (idealerweise per einfachem Einrichtungs-Bildschirm)
- Echte Termine/Tablettenplan einpflegen, Eltern das Gerät erklären
- ✅ **Ziel:** Läuft im Wohnzimmer, Peter pflegt Termine vom eigenen Handy aus

### Später / Ideen (bewusst nicht am Anfang)
- Benachrichtigung an Peter, wenn eine Tablette bis Zeitpunkt X nicht abgehakt wurde
- Fotos/Geburtstage einblenden, Wetter
- DS3231-RTC-Modul, Helligkeitssensor
- Zweites Gerät (z. B. Schlafzimmer)
- **Akustische Erinnerung per Buzzer** (15.07.2026, noch nicht umgesetzt — fehlende Bauteile;
  19.07.2026: Alternative mit Lautsprecher+Verstärker durchgerechnet, aber wegen AliExpress-
  Gesamtkosten verworfen — es bleibt vorerst beim einfachen Buzzer):
  Peter hat einen übrigen aktiven Magnet-Buzzer (Mainboard-Ersatzteil, 2 Adern, Polung "+"
  markiert) von einem Mainboard übrig. Anschluss geplant am "Sensor AD"-Header (J8) des
  Waveshare-Boards → GPIO6, laut Schaltplan der einzige dort exklusiv herausgeführte, sonst
  unbenutzte Pin. Geplante Schaltung: GPIO6 über 1kΩ an die Basis eines NPN-Transistors
  (z. B. S8050, wird auf dem Board selbst schon verwendet), Buzzer "+" an 3V3, Buzzer "−" an
  den Kollektor, Emitter an GND, Freilaufdiode (1N4148) parallel zum Buzzer wegen der Spule.
  Software: GPIO6 als normaler Digitalausgang, einfaches An/Aus genügt (aktiver Buzzer hat
  eigenen Oszillator). Steht im Widerspruch zur bisherigen "komplett stumm"-Entscheidung
  (Abschnitt 4). Die Ein/Aus-Einstellung dafür existiert bereits vorbereitet im neuen
  Einstellungen-Menü (siehe Phase 5) — wirkt erst, sobald der Buzzer tatsächlich verbaut ist.
  **Mögliche spätere Alternative:** Lautsprecher statt Buzzer für einen weicheren Ton/eine
  Melodie statt schrillem Piepen — PAM8403-Mini-Verstärkermodul (Class D, 2×3W) + 8-Ohm-
  Lautsprecher (z. B. "8ohm 3W speaker 40mm"), am selben Sensor-AD-Header angeschlossen
  (GPIO6 dann als LEDC-PWM-Tonausgang statt reinem An/Aus, 3V3/GND vom Header — Verstärker
  müsste einen weiten Versorgungsbereich bis mind. 3,3V unterstützen). Zurückgestellt wegen
  AliExpress-Gesamtkosten der Zusatzteile (Stand 19.07.2026).

---

## 4. Getroffene Entscheidungen *(Stand: 10.07.2026)*

1. **Kalender-Dienst:** Google Kalender — Pflege am Notebook und unterwegs per iPhone
   (Google-Kalender-App bzw. Kalender-Abo auf dem iPhone).
2. **WLAN bei den Eltern:** vorhanden. ✓
3. **Touch:** Tabletten sind per Touch **abhakbar** (Schiebeschalter im "Heute"-Fenster, siehe
   Phase 3). Eine Ein-/Ausschalt-Einstellung für den Fall, dass es die Eltern überfordert, ist
   *(Stand 15.07.2026)* noch nicht umgesetzt.
4. **Ton:** komplett stumm. Erinnerung ausschließlich über farbliche Hervorhebung.
