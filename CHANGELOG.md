# Änderungen

Was sich von Version zu Version **am Gerät** ändert — gedacht als Blick vor dem
Aufspielen eines Updates. Neueste Version oben.

Diese Datei ist bewusst kurz gehalten. Wer wissen will, *warum* etwas so gelöst
wurde, findet die Entwicklungsgeschichte in [FAHRPLAN.md](FAHRPLAN.md) und die
gelösten Probleme mit Ursache und Lösung in
[FALLSTRICKE_UND_WORKAROUNDS.md](FALLSTRICKE_UND_WORKAROUNDS.md).

Der Release-Workflow zieht den Abschnitt zur jeweiligen Version automatisch als
Release-Text heraus und legt diese Datei im Download-Repo
[seniorenuhr-firmware](https://github.com/peterm2024/seniorenuhr-firmware) ab.
Fehlt der Abschnitt zu einem Tag, bricht der Release-Build ab.

## v0.10.0 — 18.08.2026

- **Oberfläche auf Deutsch und Englisch umschaltbar**, Sprachknopf im
  Einstellungen-Menü. Die Tabletten-Kennungen im Kalender (`TABLETTE:`, `PILL:`,
  …) gelten sprachunabhängig weiter — ein Sprachwechsel entwertet bestehende
  Kalendereinträge nicht.
- **Späte Tabletten fallen nicht mehr durch Mitternacht.** Eine für 23:00
  vorgesehene Tablette bleibt bis 04:00 Uhr sichtbar und abhakbar, statt um
  0:00 Uhr ersatzlos zu verschwinden.
- **Tabletten-Rückblick** im Einstellungen-Menü: die letzten 30 Tage,
  Auffälligkeiten zuerst.
- **Neustart-Knopf** im Einstellungen-Menü — vorher half nur der Stecker.
- **Update-Pfeil erscheint nur noch bei tatsächlich neuerer Version.** Vorher
  meldete das Gerät auch dann ein Update, wenn die angebotene Version älter war.
- **Zweiter Weg ins Einstellungen-Menü:** fünf Sekunden auf die Status-Symbole
  halten.
- Die Hauptanzeige blitzt beim Einschalten nicht mehr kurz ungedimmt auf.

## v0.9.3 — 09.08.2026

- **Bewährungsprobe nach einem Update repariert.** Sie blockierte den
  Update-Task und hätte fehlerfreie Updates wieder zurückgerollt.
- **Update-Dialog:** Der Text passt jetzt ins Fenster, und ein Tastendruck gibt
  sofort Rückmeldung statt scheinbar ins Leere zu laufen.
- Bildschirmfotos überstehen einen Neustart ohne Kabel (eigene Flash-Partition,
  mit Zeitstempel). Betrifft nur das Entwicklungsboard.

## v0.9.2 — 09.08.2026

- **Ursache der monatelangen Verbindungsabbrüche behoben.** Ein dauerhaft
  laufender Webserver samt mDNS band den knappen internen Speicher und ließ die
  Namensauflösung scheitern; beides läuft jetzt nur noch auf Zuruf.
- **Absturzprotokoll wandert in den Flash** und übersteht damit Neustart und
  Stromausfall — auswertbar beim nächsten Anstecken.
- Einstellungen-Menü über langes Halten erreichbar; der Kalender wird beim Start
  rechtzeitig abgerufen.
- Update-Knöpfe immer sichtbar, Status-Symbole senkrecht gestapelt.
- Bei bereits bekannten Netzen ist die Passworteingabe optional.

## v0.9.1 — 08.08.2026

- **OTA-Download repariert** — Updates ließen sich in v0.9.0 nicht herunterladen.
- **Zugangsdaten für Updates liegen im NVS statt in der Firmware**, damit sie ein
  Update überstehen. Eine ungültige Adresse führt nicht mehr zum Absturz.
- Einstellungen-Menü zuverlässig erreichbar, Update-Knopf mit Rückmeldung.

## v0.9.0 — 08.08.2026

Erste über OTA verteilte Fassung.

- **Firmware-Updates über GitHub.** Das Gerät prüft selbst auf neue Versionen;
  installiert wird erst nach Bestätigung am Gerät.
- **Erinnerungsfenster für fällige Tabletten** mit Checkliste und Zeitfenster für
  die Einnahme; bestätigt wird erst mit „OK".
- **„Heute"-Fenster** mit Bildlaufleiste und Notizen aus der Kalender-Beschreibung.
- Verpasste Einnahme farblich erkennbar (Gold beziehungsweise Rot).
- **Status-Fenster für WLAN, Zeit und Kalender** per Tipp, mit erneutem Abgleich.
- Analoge Zusatzuhr, per Tipp groß/klein tauschbar.
- **Nächtlicher Absturz um Punkt 0:00 Uhr behoben.**
- Nach einem echten Absturz erscheint beim Neustart eine Diagnose-Meldung.
