# Laedt den Google-Kalender der Seniorenuhr herunter (nach kalender.ics)
# und zeigt an, was die Uhr heute anzeigen wuerde.
#
# Die geheime Kalender-Adresse steht in kalender-url.secret (nicht in Git).
$ErrorActionPreference = 'Stop'

$wurzel = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$urlDatei = Join-Path $wurzel "kalender-url.secret"
if (-not (Test-Path $urlDatei)) {
    throw "kalender-url.secret fehlt - bitte die private iCal-Adresse dort hineinschreiben"
}
$url = (Get-Content $urlDatei -First 1).Trim()

$ziel = Join-Path $wurzel "kalender.ics"
curl.exe -s -L -o $ziel $url
if ($LASTEXITCODE -ne 0) { throw "Download fehlgeschlagen" }

$anzahl = (Select-String -Path $ziel -Pattern "BEGIN:VEVENT" -AllMatches | Measure-Object).Count
Write-Host "Heruntergeladen: kalender.ics ($anzahl Eintraege insgesamt)"

$zeige = Join-Path $wurzel "test_host\zeige_tag.exe"
if (Test-Path $zeige) {
    & $zeige $ziel $(if ($args.Count -ge 1) { $args[0] })
} else {
    Write-Host "Hinweis: erst test_host\teste.ps1 ausfuehren, dann zeigt dieses Skript auch die Tagesansicht."
}
