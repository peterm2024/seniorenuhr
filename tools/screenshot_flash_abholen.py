"""
screenshot_flash_abholen.py — Liest die "screenshots"-Ringpuffer-Partition
(siehe partitions.csv, main/screenshot_speicher.c) komplett vom Geraet und
schreibt jeden belegten Platz als BMP+PNG. Gedacht fuer den Fall, dass
Screenshots ohne mitlaufenden seriellen Monitor entstanden sind (z.B. am
Router, Board an einer Powerbank) - die Aufnahmen ueberleben Neustart und
Stromausfall in dieser Partition und werden erst beim naechsten Anstecken
abgeholt (gleiches Prinzip wie der Core-Dump, siehe ENTWICKLUNG.md).

Aufruf:
    python tools/screenshot_flash_abholen.py COM5 [zielordner]

Liest die Partition per "esptool read_flash" in eine temporaere Datei,
decodiert danach alle gueltigen Plaetze (Magic-Pruefung, siehe MAGIC unten -
muss zu main/screenshot_speicher.c passen) und schreibt sie sortiert nach
Aufnahme-Reihenfolge (Sequenznummer) als "screenshot_<sequenz>.bmp" +
zugehoerige PNGs.

Die Partition ist ein Ringpuffer: nach 15 Aufnahmen wird wieder bei Platz 0
begonnen, aeltere Aufnahmen dort werden dann ueberschrieben - dieses Skript
liest daher immer nur die AKTUELL noch vorhandenen Plaetze.
"""
import sys
import os
import struct
import subprocess
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from screenshot_gemeinsam import bmp_bytes_bauen, pngs_erzeugen

# Muss zu partitions.csv und main/screenshot_speicher.c passen.
PARTITION_OFFSET = 0x690000
PARTITION_GROESSE = 1440 * 1024
PLATZ_GROESSE = 96 * 1024
KOPF_GROESSE = 32
MAGIC = 0x31485353  # "SSH1", little-endian
KOPF_FORMAT = "<IIIHHB"  # magic, sequenz, datengroesse, breite, hoehe, komprimiert
KOPF_FORMAT_GROESSE = struct.calcsize(KOPF_FORMAT)


def main():
    if len(sys.argv) < 2:
        print("Aufruf: python screenshot_flash_abholen.py <COM-Port> [zielordner]")
        sys.exit(1)
    port = sys.argv[1]
    zielordner = sys.argv[2] if len(sys.argv) > 2 else "."
    os.makedirs(zielordner, exist_ok=True)

    fd, dumpdatei = tempfile.mkstemp(suffix=".bin")
    os.close(fd)
    try:
        print(f"Lese Partition (0x{PARTITION_OFFSET:x}, {PARTITION_GROESSE} Byte) von {port}...")
        # --baud explizit: der Standard-Wert von esptool las 1,4 MB in gut
        # zwei Minuten, mit 460800 dauert derselbe Lesevorgang nur ~35s.
        subprocess.run(
            [sys.executable, "-m", "esptool", "--port", port, "--baud", "460800", "read_flash",
             str(PARTITION_OFFSET), str(PARTITION_GROESSE), dumpdatei],
            check=True,
        )
        with open(dumpdatei, "rb") as f:
            rohdaten = f.read()
    finally:
        os.remove(dumpdatei)

    anzahl_plaetze = PARTITION_GROESSE // PLATZ_GROESSE
    gefunden = []
    for i in range(anzahl_plaetze):
        start = i * PLATZ_GROESSE
        kopf_bytes = rohdaten[start:start + KOPF_FORMAT_GROESSE]
        if len(kopf_bytes) < KOPF_FORMAT_GROESSE:
            continue
        magic, sequenz, datengroesse, breite, hoehe, komprimiert = struct.unpack(KOPF_FORMAT, kopf_bytes)
        if magic != MAGIC:
            continue  # geloeschter/nie beschriebener Platz (Flash-Erase-Wert 0xFF)
        if datengroesse > PLATZ_GROESSE - KOPF_GROESSE:
            print(f"WARNUNG: Platz {i} (Sequenz {sequenz}) hat eine unplausible "
                  f"Groessenangabe ({datengroesse} Byte) - uebersprungen.")
            continue
        daten_start = start + KOPF_GROESSE
        block = rohdaten[daten_start:daten_start + datengroesse]
        gefunden.append((sequenz, i, breite, hoehe, bool(komprimiert), block))

    if not gefunden:
        print("Keine gueltigen Screenshots in der Partition gefunden.")
        return

    gefunden.sort(key=lambda eintrag: eintrag[0])
    print(f"{len(gefunden)} Screenshot(s) gefunden.")

    for sequenz, platz, breite, hoehe, komprimiert, block in gefunden:
        basis = os.path.join(zielordner, f"screenshot_{sequenz:04d}")
        bmp_datei = basis + ".bmp"
        daten = bmp_bytes_bauen(block, breite, hoehe, komprimiert)
        with open(bmp_datei, "wb") as f:
            f.write(daten)
        print(f"  Platz {platz}, Sequenz {sequenz}: {bmp_datei} ({len(daten)} Byte, {breite}x{hoehe})")
        pngs_erzeugen(bmp_datei)


if __name__ == "__main__":
    main()
