# Bildschirmfoto-Sammlung

Screenshots der einzelnen Bildschirme fuer die Projekt-Doku - aufgenommen
mit dem Entwicklungswerkzeug in `main/screenshot_debug.c` (Button unten
mittig auf dem Geraet, Ausgabe per Base64 ueber die serielle Verbindung,
dekodiert mit `tools/screenshot_dekodieren.py`).

Neue Aufnahme entsteht bei jeder wesentlichen GUI-Aenderung, nicht
automatisch/regelmaessig.

## Namenskonvention

Pro Aufnahme zwei Dateien (beide vom `tools/screenshot_dekodieren.py` automatisch
aus der BMP-Rohaufnahme erzeugt, sofern Pillow installiert ist):

- `<bildschirm>_<datum-jjjj-mm-tt>.png` — 1:1 Pixel-Rohdaten (800x480)
- `<bildschirm>_<datum-jjjj-mm-tt>_proportional.png` — auf die REALEN
  Bildschirm-Proportionen gestreckt (852x480)

Hintergrund: das 7"-Waveshare-Panel hat keine quadratischen Pixel (Peters
Nachmessung: 153,5mm x 86,5mm, Seitenverhaeltnis 16:9, waehrend die 800x480-
Ansteuerung nur 5:3 liefert - jedes Pixel ist real ca. 6,5% breiter als hoch).
Die Rohdaten-Version wirkt deshalb auf einem normalen Monitor leicht
"gestaucht"; die "_proportional"-Version ist nur fuers Betrachten/die Doku
entsprechend gestreckt, keine zweite echte Aufnahme - beide Varianten bleiben
erhalten.

## Aktueller Stand

- `hauptanzeige_2026-07-21.png` / `..._proportional.png` — Hauptanzeige
  (Nacht-Modus), erste erfolgreiche Aufnahme nach Behebung aller
  Darstellungsfehler im Screenshot-Werkzeug (siehe
  FALLSTRICKE_UND_WORKAROUNDS.md #19)
- `startbildschirm_2026-07-21.png` / `..._proportional.png` — Startbildschirm
  waehrend des Bootens (WLAN verbunden, Uhrzeit-Synchronisation laeuft gerade
  mit Countdown-Ring, Kalender-Schritt steht noch aus). Aufgenommen auf dem
  zweiten Board nach Behebung des 121px-Versatzes (FALLSTRICKE #19,
  Fallstrick E)
- `tagesansicht_2026-08-18.png` / `..._proportional.png` — Tagesansicht-Dialog
  ("Heute", 18. August 2026) im ueberarbeiteten Layout (Ausbaustufe 2):
  Liste statt Einzelfenster, Checkbox pro Tablette statt Kippschalter,
  Aenderungen werden erst mit "OK" uebernommen ("Abbrechen"/X verwerfen sie
  wieder). Ersetzt die aeltere Aufnahme vom 22. Juli 2026 (zeigte noch die
  Kippschalter-Fassung). Aufgenommen auf Board 1 (Entwicklungsboard)
- `einstellungen_2026-08-18.png` / `..._proportional.png` — Einstellungen-Menue
  mit dem neuen Sprachknopf ("Sprache: Deutsch/English", umschaltbar ohne
  Neuflashen). Aufgenommen auf Board 1 (Entwicklungsboard)
