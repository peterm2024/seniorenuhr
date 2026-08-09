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

Ohne Zielordner landen die Bilder in "screenshots_lokal/<Port>/" - ein
gitignorierter Sammelordner (je Board ein Unterordner, weil die
Sequenznummern der Boards unabhaengig voneinander zaehlen und sich sonst
ueberschreiben wuerden). Bewusst NICHT docs/screenshots/: dort liegen die
ausgewaehlten Doku-Bilder, die ins Repo gehoeren, waehrend hier ungefiltert
alles landet, was gerade auf dem Schirm stand - auch echte Termine und
Tablettennamen von den Eltern.

Liest die Partition per "esptool read_flash" in eine temporaere Datei,
decodiert danach alle gueltigen Plaetze (Magic-Pruefung, siehe MAGIC unten -
muss zu main/screenshot_speicher.c passen) und schreibt sie sortiert nach
Aufnahme-Reihenfolge (Sequenznummer) als "screenshot_<sequenz>_<zeit>.bmp" +
zugehoerige PNGs.

Die Partition ist ein Ringpuffer: nach 15 Aufnahmen wird wieder bei Platz 0
begonnen, aeltere Aufnahmen dort werden dann ueberschrieben - dieses Skript
liest daher immer nur die AKTUELL noch vorhandenen Plaetze.

Korrelation mit dem seriellen Log (Peters Wunsch): jeder Platz traegt zwei
Zeitangaben (siehe platz_kopf_t in main/screenshot_speicher.c):
  - Boot-Zeit in Millisekunden - exakt dieselbe Zahl, die ESP-IDF auch dem
    "I (...)"-Praefix jeder Log-Zeile voranstellt. Stammt der Screenshot aus
    DERSELBEN Boot-Sitzung wie ein serieller Mitschnitt, findet man die
    passende Stelle direkt per Suche nach "I (<Boot-Zeit>)" im Log.
  - Uhrzeit (Wanduhr, UTC) - bleibt auch ueber einen Neustart hinweg
    vergleichbar, im Gegensatz zur Boot-Zeit. "~" davor bedeutet: die Uhr war
    beim Aufnehmen nicht NTP-bestaetigt, nur ein Naeherungswert (siehe
    zeit_ist_manuell_gesetzt() in main/zeit.h). Fehlt ganz, wenn die Uhr beim
    Aufnehmen noch nie gestellt war.
"""
import sys
import os
import struct
import subprocess
import tempfile
from datetime import datetime, timezone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from screenshot_gemeinsam import bmp_bytes_bauen, pngs_erzeugen

# Muss zu partitions.csv und main/screenshot_speicher.c passen.
PARTITION_OFFSET = 0x690000
PARTITION_GROESSE = 1440 * 1024
PLATZ_GROESSE = 128 * 1024
KOPF_GROESSE = 32
MAGIC = 0x31485353  # "SSH1", little-endian
# magic, sequenz, datengroesse, breite, hoehe, komprimiert, boot_millis, unix_zeit, zeit_manuell
KOPF_FORMAT = "<IIIHHBIIB"
KOPF_FORMAT_GROESSE = struct.calcsize(KOPF_FORMAT)


def main():
    if len(sys.argv) < 2:
        print("Aufruf: python screenshot_flash_abholen.py <COM-Port> [zielordner]")
        sys.exit(1)
    port = sys.argv[1]
    if len(sys.argv) > 2:
        zielordner = sys.argv[2]
    else:
        # Standardziel relativ zur Projektwurzel (eine Ebene ueber tools/),
        # damit der Aufruf aus jedem Arbeitsverzeichnis dieselbe Ablage trifft.
        wurzel = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        zielordner = os.path.join(wurzel, "screenshots_lokal", port.replace(":", "_"))
    os.makedirs(zielordner, exist_ok=True)
    print(f"Zielordner: {zielordner}")

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
        (magic, sequenz, datengroesse, breite, hoehe, komprimiert,
         boot_millis, unix_zeit, zeit_manuell) = struct.unpack(KOPF_FORMAT, kopf_bytes)
        if magic != MAGIC:
            continue  # geloeschter/nie beschriebener Platz (Flash-Erase-Wert 0xFF)
        if datengroesse > PLATZ_GROESSE - KOPF_GROESSE:
            print(f"WARNUNG: Platz {i} (Sequenz {sequenz}) hat eine unplausible "
                  f"Groessenangabe ({datengroesse} Byte) - uebersprungen.")
            continue
        # datengroesse == 0: main/screenshot_speicher.c hat hier bewusst einen
        # "zu gross fuer einen Platz"-Platzhalter abgelegt (siehe dort) - Bild
        # fehlt, Sequenznummer/Zeit bleiben aber sichtbar statt einer stillen
        # Luecke.
        daten_start = start + KOPF_GROESSE
        block = rohdaten[daten_start:daten_start + datengroesse] if datengroesse else None
        gefunden.append((sequenz, i, breite, hoehe, bool(komprimiert), boot_millis,
                          unix_zeit, bool(zeit_manuell), block))

    if not gefunden:
        print("Keine gueltigen Screenshots in der Partition gefunden.")
        return

    gefunden.sort(key=lambda eintrag: eintrag[0])
    print(f"{len(gefunden)} Eintrag/Eintraege gefunden.")

    for (sequenz, platz, breite, hoehe, komprimiert, boot_millis,
         unix_zeit, zeit_manuell, block) in gefunden:
        if unix_zeit:
            zeit_utc = datetime.fromtimestamp(unix_zeit, tz=timezone.utc)
            zeit_text = zeit_utc.strftime("%Y-%m-%d %H:%M:%S UTC")
            zeit_dateiname = zeit_utc.strftime("%Y%m%d-%H%M%S")
            if zeit_manuell:
                zeit_text = "~" + zeit_text + " (nicht NTP-bestaetigt, nur Naeherung)"
        else:
            zeit_text = "Uhrzeit beim Aufnehmen unbekannt (noch kein Zeit-Sync)"
            zeit_dateiname = f"boot+{boot_millis}ms"

        if block is None:
            print(f"  Platz {platz}, Sequenz {sequenz}: UEBERSPRUNGEN (Bild passte nicht in einen "
                  f"Platz, siehe main/screenshot_speicher.c PLATZ_GROESSE)")
            print(f"    Aufgenommen: {zeit_text} - Boot-Zeit {boot_millis} ms "
                  f"(im seriellen Log nach \"I ({boot_millis})\" suchen, falls aus derselben Sitzung)")
            continue

        basis = os.path.join(zielordner, f"screenshot_{sequenz:04d}_{zeit_dateiname}")
        bmp_datei = basis + ".bmp"
        daten = bmp_bytes_bauen(block, breite, hoehe, komprimiert)
        with open(bmp_datei, "wb") as f:
            f.write(daten)
        print(f"  Platz {platz}, Sequenz {sequenz}: {bmp_datei} ({len(daten)} Byte, {breite}x{hoehe})")
        print(f"    Aufgenommen: {zeit_text} - Boot-Zeit {boot_millis} ms "
              f"(im seriellen Log nach \"I ({boot_millis})\" suchen, falls aus derselben Sitzung)")
        pngs_erzeugen(bmp_datei)


if __name__ == "__main__":
    main()
