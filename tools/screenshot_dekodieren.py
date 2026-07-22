"""
screenshot_dekodieren.py — Zieht ein per Screenshot-Debug-Button (siehe
main/screenshot_debug.c) aufgenommenes Bildschirmfoto aus einem seriellen
Log-Mitschnitt und schreibt es als BMP- und PNG-Datei(en).

Ablauf:
  1. idf.py -p COM3 monitor (oder aehnliches) mitlaufen lassen und in eine
     Log-Datei umleiten, waehrend auf dem Geraet der "Screenshot"-Button
     unten mittig angetippt wird. Die Uebertragung dauert 2-3 Minuten
     (Base64 ueber 115200 Baud) - danach zeigt der Button wieder
     "Screenshot" statt "Sende...".
  2. python screenshot_dekodieren.py <log-datei> <ziel.bmp>

Erzeugt aus <ziel.bmp> zusaetzlich (falls Pillow installiert ist) zwei PNGs:
  - <ziel>.png              — 1:1 Pixel-Rohdaten (800x480), unveraendert
  - <ziel>_proportional.png — horizontal gestreckt auf die REALEN
                              Bildschirm-Proportionen

Versatz-Selbstkontrolle: das Geraet rendert beim Aufnehmen eine schmale
Magenta-Linie an eine feste Spalte mit ins Bild (siehe screenshot_debug.c).
Dieses Skript prueft, ob sie an ihrer Soll-Spalte ankommt - wenn nicht, meldet
es den gemessenen Versatz (Render- ODER Uebertragungsfehler), schiebt das Bild
zurueck und rechnet die Marke danach wieder heraus. Aeltere Aufnahmen ohne
Marke werden unveraendert weiterverarbeitet.

Hintergrund: das 7"-Waveshare-Panel hat KEINE quadratischen Pixel. Peter hat
den Bildschirm nachgemessen: 153,5 mm x 86,5 mm (Seitenverhaeltnis 1,775,
praktisch 16:9), waehrend die Ansteuerung nur 800x480 Pixel (Seitenverhaeltnis
1,667, 5:3) liefert - jedes Pixel ist real ca. 6,5% breiter als hoch. Die
800x480-BMP/erste PNG-Datei ist deshalb ein exakter, aber auf einem normalen
(quadratische Pixel) Monitor leicht "gestaucht" wirkender 1:1-Rohdatenauszug;
die "_proportional"-Version ist nur fuers Betrachten/die Doku entsprechend
gestreckt, keine zweite echte Aufnahme.

Hinweis: auf Systemen mit mehreren Python-Installationen ggf. gezielt
"python3" statt "python" aufrufen - eine venv-Installation lieferte hier
beim Testen ohne ersichtlichen Fehler nur einen abgeschnittenen Teil der
Bilddaten zurueck.
"""
import sys
import re
import base64
import os

# Peters Nachmessung des realen Waveshare-7"-Panels (siehe Docstring oben).
PANEL_BREITE_MM = 153.5
PANEL_HOEHE_MM = 86.5

# Versatz-Selbstkontrolle: main/screenshot_debug.c rendert beim Aufnehmen eine
# schmale Magenta-Linie an eine feste Spalte mit ins Bild. Findet sie sich hier
# nicht an ihrer Soll-Spalte wieder, hat ein Versatz stattgefunden (beim Zeichnen
# ODER in der seriellen Uebertragung). Werte muessen zu den REFERENZ_MARKE_*-
# Defines in screenshot_debug.c passen.
REFERENZ_MARKE_X = 10
REFERENZ_MARKE_BREITE = 2


def referenzmarke_pruefen_und_entfernen(bild, Image):
    """Sucht die mitgerenderte Magenta-Referenzmarke, meldet einen erkannten
    Versatz, schiebt das Bild bei Bedarf horizontal zurueck und rechnet die
    Marke danach wieder heraus. Aeltere Aufnahmen ohne Marke bleiben
    unveraendert (Rueckwaertskompatibilitaet)."""
    try:
        import numpy as np
    except ImportError:
        print("HINWEIS: numpy fehlt - Referenzmarken-Pruefung uebersprungen.")
        return bild

    arr = np.asarray(bild, dtype=np.uint8)
    hoehe = arr.shape[0]
    r = arr[:, :, 0].astype(np.int16)
    g = arr[:, :, 1].astype(np.int16)
    b = arr[:, :, 2].astype(np.int16)
    ist_magenta = (r > 180) & (g < 80) & (b > 180)
    treffer_pro_spalte = ist_magenta.sum(axis=0)

    # Eine echte Marke fuellt praktisch die volle Bildhoehe - so werden
    # vereinzelte magentafarbene UI-Pixel nicht faelschlich als Marke gewertet.
    if treffer_pro_spalte.max() < hoehe * 0.5:
        print("HINWEIS: keine Referenzmarke gefunden (aelteres Bild ohne Marke oder "
              "stark gestoerte Uebertragung) - keine Versatz-Pruefung.")
        return bild

    gefunden_x = int(treffer_pro_spalte.argmax())
    versatz = gefunden_x - REFERENZ_MARKE_X
    arr = arr.copy()

    if versatz == 0:
        print(f"OK: Referenzmarke an Soll-Position x={REFERENZ_MARKE_X} - kein Versatz.")
    else:
        print(f"WARNUNG: Referenzmarke bei x={gefunden_x} statt x={REFERENZ_MARKE_X} - "
              f"Versatz von {versatz}px erkannt!")
        arr = np.roll(arr, -versatz, axis=1)
        print(f"  -> Bild um {-versatz}px horizontal zurueckgeschoben (exakt bei einem "
              f"Render-Versatz, best effort bei Uebertragungs-Verrutscher).")

    # Marke (jetzt bei x=REFERENZ_MARKE_X) durch die Spalte links daneben
    # ersetzen - bei 2px optisch unauffaellig.
    for dx in range(REFERENZ_MARKE_BREITE):
        arr[:, REFERENZ_MARKE_X + dx, :] = arr[:, REFERENZ_MARKE_X - 1, :]

    return Image.fromarray(arr, "RGB")

def main():
    logdatei = sys.argv[1] if len(sys.argv) > 1 else "boot_screendbg.log"
    zieldatei = sys.argv[2] if len(sys.argv) > 2 else "screenshot.bmp"

    with open(logdatei, "r", encoding="utf-8", errors="replace") as f:
        inhalt = f.read()

    start_muster = re.search(r"-----BEGIN SCREENSHOT (\d+)x(\d+)-----\r?\n", inhalt)
    if not start_muster:
        print("Kein 'BEGIN SCREENSHOT'-Marker gefunden - wurde der Screenshot-Button angetippt?")
        sys.exit(1)

    start = start_muster.end()
    ende = inhalt.find("-----END SCREENSHOT-----", start)
    if ende == -1:
        print("Kein 'END SCREENSHOT'-Marker gefunden - Aufzeichnung war evtl. zu kurz.")
        sys.exit(1)

    block = inhalt[start:ende]
    # Andere Tasks koennen waehrend der Pausen (vTaskDelay im Sende-Loop) eigene
    # Log-Zeilen dazwischenschreiben (z.B. "I (12345) netz: ..."). Nur Zeilen
    # behalten, die KOMPLETT aus gueltigen Base64-Zeichen bestehen - eine
    # fremde Log-Zeile enthaelt so gut wie immer Leerzeichen/Klammern/Doppel-
    # punkte und faellt dadurch als GANZE Zeile raus, statt nur teilweise
    # durchzurutschen.
    base64_zeichen = re.compile(r"^[A-Za-z0-9+/=]+$")
    zeilen = [z.strip() for z in block.splitlines()]
    b64 = "".join(z for z in zeilen if z and base64_zeichen.match(z))

    daten = base64.b64decode(b64)
    breite, hoehe = int(start_muster.group(1)), int(start_muster.group(2))

    # Fehlen am Ende Bytes (z.B. weil die Uebertragung vorzeitig endete oder die
    # letzten Base64-Zeilen verloren gingen), mit Schwarz auffuellen, damit das BMP
    # trotzdem gueltig und lesbar bleibt (24bpp = 3 Byte/Pixel + 54 Byte Kopf).
    soll_groesse = 54 + breite * 3 * hoehe
    if len(daten) < soll_groesse:
        fehlend = soll_groesse - len(daten)
        daten += b"\x00" * fehlend
        print(f"HINWEIS: {fehlend} Byte fehlten am Ende - mit Schwarz aufgefuellt "
              f"(Uebertragung evtl. unvollstaendig).")

    with open(zieldatei, "wb") as f:
        f.write(daten)

    print(f"OK: {zieldatei} geschrieben ({len(daten)} Byte, {breite}x{hoehe})")

    try:
        from PIL import Image
    except ImportError:
        print("Pillow nicht installiert - PNG-Varianten uebersprungen (pip install Pillow).")
        sys.exit(0)

    basis, _ = os.path.splitext(zieldatei)
    bild = Image.open(zieldatei).convert("RGB")

    # Versatz-Selbstkontrolle anhand der mitgerenderten Referenzmarke, bevor die
    # PNGs entstehen (siehe Kommentar bei REFERENZ_MARKE_X oben).
    bild = referenzmarke_pruefen_und_entfernen(bild, Image)

    png_roh = basis + ".png"
    bild.save(png_roh)
    print(f"OK: {png_roh} geschrieben (1:1 Rohdaten, {breite}x{hoehe})")

    streck_faktor = (PANEL_BREITE_MM / PANEL_HOEHE_MM) / (breite / hoehe)
    proportional_breite = round(breite * streck_faktor)
    png_proportional = basis + "_proportional.png"
    bild.resize((proportional_breite, hoehe), Image.LANCZOS).save(png_proportional)
    print(f"OK: {png_proportional} geschrieben (fuers Betrachten auf reale Proportionen "
          f"gestreckt, {proportional_breite}x{hoehe})")


if __name__ == "__main__":
    main()
