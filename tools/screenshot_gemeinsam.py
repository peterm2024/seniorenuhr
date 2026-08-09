"""
screenshot_gemeinsam.py — Gemeinsame Dekodier-Logik fuer beide Abhol-Wege
eines Bildschirmfotos (main/screenshot_debug.c):

  - screenshot_dekodieren.py: seriell mitgeschnittenes Base64 direkt vom
    laufenden Geraet (nur waehrend ein Monitor mitlaeuft).
  - screenshot_flash_abholen.py: aus der "screenshots"-Ringpuffer-Partition
    (main/screenshot_speicher.c) ausgelesen - funktioniert auch, wenn die
    Aufnahme(n) ohne mitlaufenden Monitor entstanden sind (Board am Router).

Beide liefern denselben Rohblock (54-Byte-BMP-Kopf + roh- oder RLE-
komprimierte Pixeldaten, siehe BMP_HEADER_GROESSE in screenshot_debug.c) -
ab hier ist die Weiterverarbeitung (RLE-Dekompression, Referenzmarken-
Pruefung, PNG-Export) identisch.
"""
import os

# Peters Nachmessung des realen Waveshare-7"-Panels (siehe
# screenshot_dekodieren.py-Docstring fuer den Hintergrund).
PANEL_BREITE_MM = 153.5
PANEL_HOEHE_MM = 86.5

BMP_HEADER_GROESSE = 54

# Versatz-Selbstkontrolle: main/screenshot_debug.c rendert beim Aufnehmen eine
# schmale Magenta-Linie an eine feste Spalte mit ins Bild. Werte muessen zu
# den REFERENZ_MARKE_*-Defines dort passen.
REFERENZ_MARKE_X = 10
REFERENZ_MARKE_BREITE = 2


def rle_dekomprimieren(daten, anzahl_pixel):
    """Kehrt pixel_rle_komprimieren() aus main/screenshot_debug.c um: je
    4-Byte-Chunk (Lauflaenge 1-255 + 3 Farbbytes) werden die Farbbytes
    entsprechend oft wiederholt. Ein unvollstaendiger letzter Chunk (z.B.
    durch abgebrochene Uebertragung) wird ignoriert - das Auffuellen mit
    Schwarz uebernimmt der Aufrufer wie beim unkomprimierten Fall."""
    ausgabe = bytearray()
    soll_bytes = anzahl_pixel * 3
    pos = 0
    while pos + 4 <= len(daten) and len(ausgabe) < soll_bytes:
        lauf = daten[pos]
        pixel = daten[pos + 1:pos + 4]
        ausgabe += pixel * lauf
        pos += 4
    return bytes(ausgabe[:soll_bytes])


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


def bmp_bytes_bauen(block, breite, hoehe, komprimiert):
    """Baut aus einem Rohblock (BMP-Kopf + roh-/RLE-Pixeldaten, wie er sowohl
    seriell als auch aus der Flash-Partition ankommt) ein gueltiges,
    unkomprimiertes 24bpp-BMP. Fehlen am Ende Bytes (abgebrochene
    Uebertragung/beschaedigter Flash-Platz), wird mit Schwarz aufgefuellt."""
    kopf = block[:BMP_HEADER_GROESSE]
    pixel_rohdaten = block[BMP_HEADER_GROESSE:]

    if komprimiert:
        pixel_daten = rle_dekomprimieren(pixel_rohdaten, breite * hoehe)
    else:
        pixel_daten = pixel_rohdaten

    soll_pixel_groesse = breite * 3 * hoehe
    if len(pixel_daten) < soll_pixel_groesse:
        fehlend = soll_pixel_groesse - len(pixel_daten)
        pixel_daten += b"\x00" * fehlend
        print(f"HINWEIS: {fehlend} Byte fehlten am Ende - mit Schwarz aufgefuellt "
              f"(Aufnahme evtl. unvollstaendig).")

    return kopf + pixel_daten


def pngs_erzeugen(bmp_datei):
    """Erzeugt aus einem BMP (falls Pillow installiert ist) zwei PNGs:
      - <basis>.png              — 1:1 Pixel-Rohdaten
      - <basis>_proportional.png — auf die realen Panel-Proportionen gestreckt
    Prueft dabei die Referenzmarke wie referenzmarke_pruefen_und_entfernen().
    Gibt True zurueck, wenn Pillow verfuegbar war (PNGs also entstanden)."""
    try:
        from PIL import Image
    except ImportError:
        print("Pillow nicht installiert - PNG-Varianten uebersprungen (pip install Pillow).")
        return False

    basis, _ = os.path.splitext(bmp_datei)
    bild = Image.open(bmp_datei).convert("RGB")
    breite, hoehe = bild.size

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
    return True
