"""
screenshot_dekodieren.py — Zieht ein per Screenshot-Debug-Button (siehe
main/screenshot_debug.c) aufgenommenes Bildschirmfoto aus einem seriellen
Log-Mitschnitt und schreibt es als BMP- und PNG-Datei(en).

Ablauf:
  1. idf.py -p COM3 monitor (oder aehnliches) mitlaufen lassen und in eine
     Log-Datei umleiten, waehrend auf dem Geraet der "Screenshot"-Button
     unten mittig angetippt wird. Die Uebertragung dauert dank RLE-Kompression
     der Pixeldaten (siehe main/screenshot_debug.c) meist nur noch einen
     Bruchteil der frueheren 2-3 Minuten (Base64 ueber 115200 Baud) - danach
     zeigt der Button wieder "Screenshot" statt "Sende...".
  2. python screenshot_dekodieren.py <log-datei> <ziel.bmp>

Kein mitlaufender Monitor griffbereit (z.B. Board am Router, per Powerbank
betrieben)? Dann screenshot_flash_abholen.py verwenden - dieselbe Aufnahme
landet zusaetzlich in einer Flash-Partition und uebersteht Neustart/
Stromausfall (main/screenshot_speicher.c).

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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from screenshot_gemeinsam import bmp_bytes_bauen, pngs_erzeugen


def main():
    logdatei = sys.argv[1] if len(sys.argv) > 1 else "boot_screendbg.log"
    zieldatei = sys.argv[2] if len(sys.argv) > 2 else "screenshot.bmp"

    with open(logdatei, "r", encoding="utf-8", errors="replace") as f:
        inhalt = f.read()

    start_muster = re.search(r"-----BEGIN SCREENSHOT (RLE )?(\d+)x(\d+)-----\r?\n", inhalt)
    if not start_muster:
        print("Kein 'BEGIN SCREENSHOT'-Marker gefunden - wurde der Screenshot-Button angetippt?")
        sys.exit(1)

    komprimiert = start_muster.group(1) is not None
    breite, hoehe = int(start_muster.group(2)), int(start_muster.group(3))

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

    empfangen = base64.b64decode(b64)

    daten = bmp_bytes_bauen(empfangen, breite, hoehe, komprimiert)
    with open(zieldatei, "wb") as f:
        f.write(daten)

    print(f"OK: {zieldatei} geschrieben ({len(daten)} Byte, {breite}x{hoehe})")
    pngs_erzeugen(zieldatei)


if __name__ == "__main__":
    main()
