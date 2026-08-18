# Mitwirken

Danke fürs Interesse. Dies ist ein privates Hobbyprojekt, entstanden für die
eigenen Eltern — entsprechend gelten ein paar Eigenheiten, die man vorher
kennen sollte.

*English: contributions are welcome. Documentation, code comments and log
output are in German and should stay that way; only the user interface is
bilingual. Issues and pull requests in English are fine.*

## Bevor du Zeit investierst

Bei größeren Änderungen lohnt sich ein Issue vorab. Das Projekt hat eine
konkrete Zielgruppe (hochbetagte Menschen, ein Gerät in einer Wohnung weit
entfernt), und manches, was technisch naheliegt, passt aus diesem Grund nicht.
Kleine Fehlerkorrekturen gerne direkt als Pull Request.

## Sprache

Dokumentation, Code-Kommentare und Log-Ausgaben sind **auf Deutsch** und sollen
es bleiben — die Log-Ausgabe bewusst, sie ist das Diagnosewerkzeug des
Projekts. Zweisprachig ist nur die Bedienoberfläche: alle sichtbaren Texte
laufen über `text(TXT_...)` aus `main/texte.c`. Eine weitere Sprache heißt:
eine Spalte in dieser Tabelle ergänzen. Alles jenseits von Latin-1 erfordert
zusätzlich neu erzeugte Schriften (`tools/fonts/erzeuge_fonts.ps1`).

## Vor dem Pull Request

```powershell
test_host\teste.ps1   # ICS-Parser, Versionsvergleich, Tabletten-Protokoll
idf.py build
```

Die Host-Tests laufen ohne Hardware und decken alles ab, was sich ohne Gerät
prüfen lässt. Wenn du etwas änderst, das dort geprüft werden kann, gehört ein
Testfall dazu — und zwar einer, der **ohne** die Änderung fehlschlägt.

Ein Board zum Testen hat nicht jeder. Sag im Pull Request dazu, was du
tatsächlich ausprobiert hast und was nicht; das ist hilfreicher als eine
Vermutung.

## Wenn etwas nicht funktioniert

`FALLSTRICKE_UND_WORKAROUNDS.md` sammelt gelöste Probleme mit Ursache und
Lösung — vom Flash-Größen-Assert über LVGL-Speicherlecks bis zu Abstürzen, die
erst nach Stunden Laufzeit auftraten. Dort zuerst nachsehen, bevor du etwas
untersuchst, das schon einmal gelöst wurde.

Architektur und Entwurfsentscheidungen stehen in `FAHRPLAN.md`, die
Entwicklungsumgebung in `ENTWICKLUNG.md`, die Arbeitsweise im Repository in
`CLAUDE.md`.

## Hardware

Zielgerät ist das Waveshare ESP32-S3-Touch-LCD-7 in der **N8R8-Variante**
(8 MB Flash, 8 MB PSRAM — nicht 16 MB, wie die Produktseite nahelegt).
Gebaut wird mit ESP-IDF 5.5.

Der knappe Rohstoff ist der **interne SRAM**, nicht der PSRAM. Alles, was
dauerhaft läuft und Speicher bindet, wird kritisch gesehen — ein dauerhaft
lauschender Webserver hat hier schon einmal die Namensauflösung lahmgelegt
(Fallstrick 39).

## Lizenz

Beiträge stehen unter der GPLv3 wie das übrige Projekt.
