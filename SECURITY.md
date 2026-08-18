# Sicherheit

## Ein Sicherheitsproblem melden

Bitte **kein öffentliches Issue** anlegen, wenn es um eine Sicherheitslücke
geht. Stattdessen über GitHub unter *Security → Report a vulnerability* melden
(privater Kanal) oder direkt an den Repository-Inhaber schreiben.

Dies ist ein privates Hobbyprojekt ohne Support-Zusage. Eine Antwort kommt,
sobald es zeitlich geht — eine Frist kann ich nicht versprechen.

## Was dieses Gerät tut und was nicht

Für eine Einschätzung, ob ein Fund relevant ist:

- Die Uhr baut **ausgehende** HTTPS-Verbindungen auf: den Kalender-Abruf und
  die Update-Prüfung gegen GitHub. Zertifikate werden gegen das ESP-IDF-Bundle
  geprüft.
- Sie nimmt **keine** eingehenden Verbindungen aus dem Internet an. Es gibt
  eine lokale Weboberfläche zum Ändern der Kalender-Adresse, die aber
  standardmäßig **aus** ist und nur auf Zuruf im Einstellungsmenü eingeschaltet
  wird. Sie ist unverschlüsselt (HTTP) und ohne Anmeldung — gedacht für das
  eigene Heimnetz, für die Dauer einer Konfiguration.
- Firmware-Updates lädt das Gerät selbst von GitHub Releases. Sie werden **nie
  ohne Bestätigung am Gerät** installiert und rollen automatisch zurück, wenn
  sich die neue Version nach der Installation nicht bewährt.
- Es ist **kein Zugangstoken in der Firmware** eingebettet. Das Release-Repo
  ist öffentlich lesbar, gerade damit das Gerät ohne Token auskommt.

## Was bewusst nicht abgesichert ist

- **Wer physisch am Gerät steht, kann alles.** Das Einstellungsmenü ist nur
  durch fünf Sekunden Halten plus Bestätigung vor versehentlichem Öffnen
  geschützt — das ist eine Absicherung gegen Fehlbedienung, keine gegen
  Angreifer.
- **WLAN-Zugangsdaten und die Kalender-Adresse liegen unverschlüsselt im NVS.**
  Wer den Flash ausliest, kann sie lesen. Flash-Encryption und Secure Boot sind
  nicht aktiviert.
- Die private Kalender-Adresse ist der einzige Schutz der Termindaten — wer sie
  kennt, kann den Kalender abrufen. Das ist eine Eigenschaft des ICS-Abos, nicht
  dieses Geräts.

Diese Punkte sind für ein Gerät im eigenen Wohnzimmer bewusst so gewählt. Wer
die Firmware in einem anderen Umfeld einsetzt, sollte sie neu bewerten.

## Kein Medizinprodukt

Die Uhr ist kein Medizinprodukt und ersetzt keine geprüfte
Medikamenten-Erinnerung. Verlasst euch bei kritischen Medikamenten nie allein
auf dieses Gerät — Näheres im [README](README.md).
