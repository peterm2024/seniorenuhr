"""
screenshot_dekodieren.py — Zieht ein per Screenshot-Debug-Button (siehe
main/screenshot_debug.c) aufgenommenes Bildschirmfoto aus einem seriellen
Log-Mitschnitt und schreibt es als BMP-Datei.

Ablauf:
  1. idf.py -p COM3 monitor (oder aehnliches) mitlaufen lassen und in eine
     Log-Datei umleiten, waehrend auf dem Geraet der "Screenshot"-Button
     unten mittig angetippt wird. Die Uebertragung dauert 2-3 Minuten
     (Base64 ueber 115200 Baud) - danach zeigt der Button wieder
     "Screenshot" statt "Sende...".
  2. python screenshot_dekodieren.py <log-datei> <ziel.bmp>

Hinweis: auf Systemen mit mehreren Python-Installationen ggf. gezielt
"python3" statt "python" aufrufen - eine venv-Installation lieferte hier
beim Testen ohne ersichtlichen Fehler nur einen abgeschnittenen Teil der
Bilddaten zurueck.
"""
import sys
import re
import base64

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
with open(zieldatei, "wb") as f:
    f.write(daten)

breite, hoehe = start_muster.group(1), start_muster.group(2)
print(f"OK: {zieldatei} geschrieben ({len(daten)} Byte, {breite}x{hoehe})")
