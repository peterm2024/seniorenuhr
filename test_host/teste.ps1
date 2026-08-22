# Baut die Host-Tests mit gcc (WinLibs) und fuehrt sie aus:
#   - ICS-Parser (components/kalender)
#   - Tabletten-Langzeitprotokoll (main/tabletten_protokoll.c)
$ErrorActionPreference = 'Stop'

$gcc = Get-Command gcc -ErrorAction SilentlyContinue
if ($gcc) {
    $gcc = $gcc.Source
} else {
    $gcc = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter gcc.exe -ErrorAction SilentlyContinue |
           Select-Object -First 1 -ExpandProperty FullName
}
if (-not $gcc) { throw "gcc nicht gefunden - bitte installieren (winget install BrechtSanders.WinLibs.POSIX.UCRT)" }

$hier = Split-Path -Parent $MyInvocation.MyCommand.Path
$quelle = Join-Path $hier "..\components\kalender"

& $gcc -std=c99 -Wall -Wextra -Werror `
    "-I$quelle\include" `
    "$quelle\ics_parser.c" `
    "$hier\test_ics_parser.c" `
    -o "$hier\test_ics.exe"
if ($LASTEXITCODE -ne 0) { throw "Kompilierung fehlgeschlagen" }

# Anzeige-Werkzeug gleich mitbauen (zeige_tag <datei.ics> [JJJJ-MM-TT])
& $gcc -std=c99 -Wall -Wextra -Werror `
    "-I$quelle\include" `
    "$quelle\ics_parser.c" `
    "$hier\zeige_tag.c" `
    -o "$hier\zeige_tag.exe"
if ($LASTEXITCODE -ne 0) { throw "Kompilierung von zeige_tag fehlgeschlagen" }

# Tabletten-Protokoll: liegt in main/ und zieht ESP-Header nach, die es auf dem
# PC nicht gibt - dafuer die Stubs aus test_host/stubs. Die Dateipfade werden
# auf temporaere Namen umgebogen, damit der Test nichts Echtes anfasst.
$hauptquelle = Join-Path $hier "..\main"
& $gcc -std=c99 -Wall -Wextra -Werror `
    "-I$quelle\include" "-I$hauptquelle" "-I$hier\stubs" `
    -include "$hier\stubs\testpfade.h" `
    "$hauptquelle\tabletten_protokoll.c" `
    "$hauptquelle\protokoll_ansicht.c" `
    "$hier\test_protokoll.c" `
    -o "$hier\test_protokoll.exe"
if ($LASTEXITCODE -ne 0) { throw "Kompilierung der Protokoll-Tests fehlgeschlagen" }

# Versionsvergleich: haengt an nichts weiter als der C-Standardbibliothek.
& $gcc -std=c99 -Wall -Wextra -Werror `
    "-I$hauptquelle" `
    "$hauptquelle\version_vergleich.c" `
    "$hier\test_version.c" `
    -o "$hier\test_version.exe"
if ($LASTEXITCODE -ne 0) { throw "Kompilierung der Versionstests fehlgeschlagen" }

& "$hier\test_ics.exe"
$ergebnis_ics = $LASTEXITCODE

& "$hier\test_version.exe"
$ergebnis_version = $LASTEXITCODE

# Im temporaeren Verzeichnis laufen lassen - der Test legt Dateien an.
Push-Location $env:TEMP
& "$hier\test_protokoll.exe"
$ergebnis_protokoll = $LASTEXITCODE
Pop-Location

if ($ergebnis_ics -ne 0) { exit $ergebnis_ics }
if ($ergebnis_version -ne 0) { exit $ergebnis_version }
exit $ergebnis_protokoll
