# Erzeugt die LVGL-Fonts fuer die Seniorenuhr aus Montserrat-Bold.
#
# Die in LVGL eingebauten Montserrat-Fonts enthalten keine deutschen
# Umlaute — deshalb generieren wir eigene. Benoetigt Node.js (npx).
# Ausgabe: ..\..\assets\fonts\*.c  (werden spaeter im ESP-IDF-Projekt
# einkompiliert).
$ErrorActionPreference = 'Stop'

$hier = Split-Path -Parent $MyInvocation.MyCommand.Path
$ausgabe = Join-Path $hier "..\..\assets\fonts"
New-Item -ItemType Directory -Force $ausgabe | Out-Null

# Schrift herunterladen (einmalig)
$ttf = Join-Path $hier "Montserrat-Bold.ttf"
if (-not (Test-Path $ttf)) {
    Write-Host "Lade Montserrat-Bold.ttf herunter..."
    Invoke-WebRequest -UseBasicParsing `
        -Uri "https://github.com/JulietaUla/Montserrat/raw/master/fonts/ttf/Montserrat-Bold.ttf" `
        -OutFile $ttf
}

# Zweite Quelle NUR fuer das Hakenzeichen: Montserrat-Bold enthaelt keine
# Symbolglyphen (lv_font_conv bricht bei --range 0x2713/0x2714 mit "doesn't
# have any characters included in range" ab - nachgeprueft, nicht vermutet).
# lv_font_conv kann mehrere Schriften in EINE Ausgabe mischen, jede mit
# eigenem Bereich. Noto Sans Symbols 2 steht wie Montserrat unter der SIL
# Open Font License 1.1, die Lizenzlage bleibt damit unveraendert
# (siehe ..\..\assets\fonts\LICENSE-OFL.txt).
$symbol_ttf = Join-Path $hier "NotoSansSymbols2-Regular.ttf"
if (-not (Test-Path $symbol_ttf)) {
    Write-Host "Lade NotoSansSymbols2-Regular.ttf herunter..."
    Invoke-WebRequest -UseBasicParsing `
        -Uri "https://github.com/google/fonts/raw/main/ofl/notosanssymbols2/NotoSansSymbols2-Regular.ttf" `
        -OutFile $symbol_ttf
}

# Zeichenvorrat: ASCII + deutsche Umlaute/sz + Sonderzeichen.
# Numerisch angegeben, damit keine Codepage-Probleme entstehen:
#   0xC4/0xD6/0xDC = AE OE UE   0xE4/0xF6/0xFC = ae oe ue   0xDF = sz
#   0xB0 = Grad   0x2013 = Gedankenstrich   0x20AC = Euro
#   0x201E/0x201C/0x201D = Anfuehrungszeichen   0x2019 = Apostroph
$voll = "0x20-0x7E,0xC4,0xD6,0xDC,0xDF,0xE4,0xF6,0xFC,0xB0,0x2013,0x2019,0x201C,0x201D,0x201E,0x20AC"
# Fuer die riesige Uhrzeit reichen Ziffern, Doppelpunkt, Punkt, Leerzeichen:
$ziffern = "0x20,0x2E,0x30-0x3A"

# Aus der Symbolschrift: 0x2714 = kraeftiger Haken. Bewusst NICHT 0x2713 -
# der ist deutlich duenner und wirkt neben Montserrat Bold wie ein Fremdkoerper
# (beide Varianten als Bild verglichen). Markiert abgehakte Tabletten, siehe
# HAKEN_PRAEFIX in main/app_main.c und main/tagesansicht.c.
$symbole = "0x2714"

$fonts = @(
    @{ name = "schrift_uhr_128";   groesse = 128; bereich = $ziffern; symbole = $null },
    @{ name = "schrift_gross_72";  groesse = 72;  bereich = $voll;    symbole = $symbole },
    @{ name = "schrift_mittel_40"; groesse = 40;  bereich = $voll;    symbole = $symbole },
    @{ name = "schrift_klein_28";  groesse = 28;  bereich = $voll;    symbole = $symbole }
)

foreach ($f in $fonts) {
    $ziel = Join-Path $ausgabe "$($f.name).c"
    Write-Host "Erzeuge $($f.name) ($($f.groesse) px)..."

    # Argumentliste statt Backtick-Fortsetzungen: bei "--font A --range R1
    # --font B --range R2" gehoert jeder Bereich zur davorstehenden Schrift,
    # die Reihenfolge ist also bedeutungstragend und soll sichtbar bleiben.
    $argumente = @('--yes', 'lv_font_conv', '--font', $ttf, '--range', $f.bereich)
    if ($f.symbole) {
        $argumente += @('--font', $symbol_ttf, '--range', $f.symbole)
    }
    $argumente += @('--size', $f.groesse, '--bpp', '4', '--format', 'lvgl', '--no-compress', '-o', $ziel)

    npx @argumente
    if ($LASTEXITCODE -ne 0) { throw "lv_font_conv fehlgeschlagen fuer $($f.name)" }
}

Write-Host "Fertig. Dateien liegen in $ausgabe"
