# Baut die ICS-Parser-Tests mit gcc (WinLibs) und fuehrt sie aus.
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

& "$hier\test_ics.exe"
exit $LASTEXITCODE
