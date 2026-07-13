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
| microSD-Slot | | Offline-Cache für Termine, Konfiguration |
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
| Framework | **ESP-IDF 5.x** (VS Code-Extension) | Waveshare-Beispiele nutzen es; RGB-Display + PSRAM sauber konfigurierbar; OTA eingebaut |
| Grafik | **LVGL 8/9** | Standard für dieses Board, fertige Waveshare-Demo als Startpunkt |
| UI-Schriften | Eigene LVGL-Fonts (lv_font_conv) | Die eingebauten Fonts haben **keine deutschen Umlaute** (Mittwoch geht, „März" nicht) — wir generieren große Fonts (72–140 pt) mit ä/ö/ü/ß |
| Uhrzeit | SNTP + Zeitzone `Europe/Berlin` | Sommer-/Winterzeit automatisch (`CET-1CEST,M3.5.0,M10.5.0/3`) |
| Termine | ICS-Abruf per HTTPS + Mini-Parser | Nur Ausschnitt „heute + morgen" nötig → Parser bleibt klein |
| Speicher | NVS (WLAN-Zugang, Einstellungen), SD/LittleFS (Termin-Cache) | Übersteht Neustarts und Internetausfälle |
| Wartung | OTA-Update übers Netz | Neue Firmware ohne Besuch bei den Eltern |

### 2.3 Module der Firmware

```
seniorenuhr/
├── main/
│   ├── app_main.c          – Start, Task-Aufteilung
│   ├── display/            – LCD-Init (RGB), LVGL-Anbindung, Helligkeit
│   ├── ui/                 – Bildschirm-Layout, Fonts, Tag/Nacht-Modus
│   ├── zeit/               – SNTP, Zeitzone, Wochentag/Tageszeit-Logik
│   ├── kalender/           – ICS-Download, Parser, Cache, Tabletten-Erkennung
│   ├── netz/               – WLAN-Verbindung + automatischer Reconnect
│   └── wartung/            – OTA, Watchdog, Status-/Fehleranzeige
└── FAHRPLAN.md
```

Zwei Kernaufgaben laufen getrennt: die **UI-Task** (LVGL, jede Sekunde Uhr aktualisieren) und
die **Sync-Task** (WLAN, NTP, Kalender) — so ruckelt die Anzeige nie, egal was das Netz macht.

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

---

## 3. Fahrplan (Phasen)

Jede Phase endet mit etwas Sichtbarem, das funktioniert.

### Phase 0 — Werkzeugkasten & Hardware-Test *(½ Tag)*
- ESP-IDF + VS Code-Extension installieren, Treiber prüfen
- Waveshare-Demo (LVGL) herunterladen, kompilieren, flashen
- ✅ **Ziel:** Display zeigt Demo, Touch reagiert → Hardware ist ok, Toolchain steht

### Phase 1 — Die nackte Uhr *(1–2 Tage)*
- WLAN-Verbindung + SNTP, Zeitzone Europe/Berlin
- Anzeige: Uhrzeit, Wochentag, Datum (deutsch), Sekundentakt-Update
- ✅ **Ziel:** Eine korrekt gehende, deutsch beschriftete Uhr

### Phase 2 — Seniorengerechte Anzeige *(1–2 Tage)*
- Große eigene Fonts mit Umlauten generieren und einbinden
- Layout nach Entwurf oben, Tageszeit-Zeile, Tag/Nacht-Modus (Dimmen per Uhrzeit)
- ✅ **Ziel:** Aus 3 m Entfernung ablesbar, nachts nicht störend

### Phase 3 — Termine & Tabletten mit Testdaten *(1–2 Tage)*
- Datenmodell (Termin: Zeit, Titel, Typ) + Anzeige-Listen
- Testdaten fest einprogrammiert bzw. als JSON von SD-Karte
- Hervorhebungs-Logik: „jetzt fällige" Tablette farblich betonen
- Tabletten per Touch abhakbar (✓), Haken bleibt bis Mitternacht gespeichert —
  **per Einstellung ein-/ausschaltbar**
- ✅ **Ziel:** Der Bildschirm sieht aus wie das Endprodukt — nur die Daten sind noch statisch

### Phase 4 — Kalender-Anbindung *(2–3 Tage, kniffligste Phase)*
- Kalender bei Google/Nextcloud einrichten, geheime ICS-URL erzeugen
- HTTPS-Download + ICS-Parser (nur heute/morgen, inkl. Wiederholungen für Tabletten)
- Cache auf SD/Flash, Verhalten bei Internetausfall definieren
- ✅ **Ziel:** Termin am Handy eintragen → erscheint binnen 15 min auf der Uhr

### Phase 5 — Robustheit & Fernwartung *(1–2 Tage)*
- Watchdog, automatischer WLAN-Reconnect, sauberer Kaltstart nach Stromausfall
- OTA-Updates (Firmware aus der Ferne aktualisieren)
- Dezente Statusanzeige (kleines Symbol bei „kein WLAN" — keine Fehlermeldungen für die Eltern)
- ✅ **Ziel:** Eine Woche Dauerlauf bei Peter ohne Eingriff

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

---

## 4. Getroffene Entscheidungen *(Stand: 10.07.2026)*

1. **Kalender-Dienst:** Google Kalender — Pflege am Notebook und unterwegs per iPhone
   (Google-Kalender-App bzw. Kalender-Abo auf dem iPhone).
2. **WLAN bei den Eltern:** vorhanden. ✓
3. **Touch:** Tabletten sind per Touch **abhakbar** — die Funktion ist per Einstellung
   ein-/ausschaltbar, falls sie die Eltern überfordert.
4. **Ton:** komplett stumm. Erinnerung ausschließlich über farbliche Hervorhebung.
