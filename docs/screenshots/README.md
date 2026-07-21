# Bildschirmfoto-Sammlung

Screenshots der einzelnen Bildschirme fuer die Projekt-Doku - aufgenommen
mit dem Entwicklungswerkzeug in `main/screenshot_debug.c` (Button unten
mittig auf dem Geraet, Ausgabe per Base64 ueber die serielle Verbindung,
dekodiert mit `tools/screenshot_dekodieren.py`).

Neue Aufnahme entsteht bei jeder wesentlichen GUI-Aenderung, nicht
automatisch/regelmaessig.

## Namenskonvention

`<bildschirm>_<datum-jjjj-mm-tt>.png`, z. B. `hauptanzeige_2026-07-21.png`.
Das Werkzeug liefert BMP (unkomprimiert); vor dem Ablegen hier nach PNG
konvertieren (deutlich kleiner, z. B. per Python/Pillow: `Image.open(...).save(...)`).

## Aktueller Stand

- `hauptanzeige_2026-07-21.png` — Hauptanzeige (Nacht-Modus), erste erfolgreiche
  Aufnahme nach Behebung aller Darstellungsfehler im Screenshot-Werkzeug
  (siehe FALLSTRICKE_UND_WORKAROUNDS.md #19)
