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

# Zeichenvorrat: ASCII + deutsche Umlaute/sz + Sonderzeichen.
# Numerisch angegeben, damit keine Codepage-Probleme entstehen:
#   0xC4/0xD6/0xDC = AE OE UE   0xE4/0xF6/0xFC = ae oe ue   0xDF = sz
#   0xB0 = Grad   0x2013 = Gedankenstrich   0x20AC = Euro
#   0x201E/0x201C/0x201D = Anfuehrungszeichen   0x2019 = Apostroph
# Kein Haken-Unicode-Zeichen (0x2713 o.ae.) - Montserrat-Bold enthaelt keine
# solchen Symbolglyphen (lv_font_conv bricht mit "doesn't have any characters
# included in range" ab). Abgehakte Tabletten werden stattdessen mit einem
# ASCII-Praefix "[x] " markiert (siehe app_main.c/tagesansicht.c).
$voll = "0x20-0x7E,0xC4,0xD6,0xDC,0xDF,0xE4,0xF6,0xFC,0xB0,0x2013,0x2019,0x201C,0x201D,0x201E,0x20AC"
# Fuer die riesige Uhrzeit reichen Ziffern, Doppelpunkt, Punkt, Leerzeichen:
$ziffern = "0x20,0x2E,0x30-0x3A"

$fonts = @(
    @{ name = "schrift_uhr_128";   groesse = 128; bereich = $ziffern },
    @{ name = "schrift_gross_72";  groesse = 72;  bereich = $voll },
    @{ name = "schrift_mittel_40"; groesse = 40;  bereich = $voll },
    @{ name = "schrift_klein_28";  groesse = 28;  bereich = $voll }
)

foreach ($f in $fonts) {
    $ziel = Join-Path $ausgabe "$($f.name).c"
    Write-Host "Erzeuge $($f.name) ($($f.groesse) px)..."
    npx --yes lv_font_conv `
        --font $ttf `
        --size $f.groesse `
        --bpp 4 `
        --format lvgl `
        --no-compress `
        --range $f.bereich `
        -o $ziel
    if ($LASTEXITCODE -ne 0) { throw "lv_font_conv fehlgeschlagen fuer $($f.name)" }
}

Write-Host "Fertig. Dateien liegen in $ausgabe"
