# Entwicklungs-Logbuch: Gelöste Fallstricke & Workarounds

Dieses Dokument dokumentiert die technischen Hürden, die während der Entwicklung der Seniorenuhr aufgetreten sind, sowie die erarbeiteten Lösungen und Best Practices.

---

## 1. ESP-IDF `install.ps1`/`export.ps1` verweigern den Dienst innerhalb einer aktiven Python-Virtualenv

**Problem:** Sowohl die Werkzeug-Installation (`install.ps1 esp32s3`) als auch das spätere Aktivieren der Umgebung (`export.ps1`) brachen mit `ERROR: This script was called from a virtual environment, can not create a virtual environment again` ab.
**Ursache:** Die PowerShell-Sitzung dieser Maschine startet mit einer bereits aktivierten Python-Virtualenv (`$env:VIRTUAL_ENV` zeigt auf ein fremdes venv, z. B. eines Hilfswerkzeugs). ESP-IDFs eigenes Setup will selbst ein venv unter `~/.espressif` anlegen und bricht ab, sobald es eine bereits aktive Virtualenv erkennt.
**Lösung:** Vor jedem `install.ps1`/`export.ps1`-Aufruf `Remove-Item Env:VIRTUAL_ENV -ErrorAction SilentlyContinue` ausführen. Gilt für **jede neue Shell-Sitzung**, die mit ESP-IDF arbeitet — steht auch in `ENTWICKLUNG.md`.

---

## 2. Board bootete in einer Assert-Schleife — dieses Exemplar hat 8 MB Flash, nicht 16 MB

**Problem:** Nach dem ersten Flash-Vorgang mit `sdkconfig.defaults` auf 16 MB Flash bootete das Board endlos mit `assert failed: __esp_system_init_fn_init_flash startup_funcs.c:118 (flash_ret == ESP_OK)`.
**Ursache:** Das Entwicklungsboard ist die **N8R8-Variante mit 8 MB Flash** (verifiziert per `python -m esptool --chip esp32s3 -p COM3 flash_id`: GigaDevice-Chip, 8 MB, Quad-Modus), die Firmware war aber auf 16 MB konfiguriert und versuchte damit, eine Flash-Größe zu adressieren, die physisch nicht vorhanden ist.
**Lösung:** `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y` statt `_16MB` in `sdkconfig.defaults`, Partitionstabelle (`partitions.csv`) entsprechend auf 8 MB angepasst, `sdkconfig` gelöscht und neu erzeugt.
**Lehre:** Die tatsächliche Chip-Bestückung **jedes einzelnen Boards** per `esptool flash_id` gegenprüfen, bevor die Flash-Größen-Konfiguration gesetzt wird — und von einem Exemplar niemals auf das Produkt schließen (siehe Nachtrag).

**Nachtrag (19.08.2026) — die ursprüngliche Erklärung war falsch:** Hier stand seit Juli 2026, die Waveshare-Produktseite nenne fälschlich 16 MB. Das stimmt nicht. Board 2, neu gekauft, meldet beim Booten selbst:
`W (915) spi_flash: Detected size(16384k) larger than the size in the binary image header(8192k).`
Es hat also **physisch 16 MB** und entspricht damit exakt der Spezifikation (Produktseite und Händler nennen beide `ESP32-S3N16R8` mit 16 MB Flash). Das 8-MB-Board ist Board 1, ein Gerät aus der Firma und offenbar schlicht die andere Variante — aus einem Exemplar wurde damals eine Produkteigenschaft gefolgert. Die Konfiguration bleibt auf 8 MB, dem kleinsten gemeinsamen Nenner: so läuft dieselbe Binary auf beiden Boards, auf Board 2 bleibt die obere Hälfte des Flash ungenutzt.

---

## 3. Eigene Test-Erwartung war falsch, nicht der Parser

**Problem:** Beim ersten Testlauf von `test_ics_parser.c` schlug die Prüfung „vier Einträge am 15.07." fehl — der Parser lieferte nur drei.
**Ursache:** Beim Schreiben der Testdaten und der zugehörigen Erwartung wurde ein Termin falsch mitgezählt (die Erwartung ging von vier Einträgen aus, tatsächlich treffen laut Testkalender nur drei auf diesen Tag zu). Der Parser selbst arbeitete korrekt.
**Lösung:** Testerwartung von 4 auf 3 korrigiert (`test_ics_parser.c`), alle 25 Prüfungen liefen danach grün.
**Lehre:** Testerwartungen sind genauso fehleranfällig wie der Code selbst. Bei einer Diskrepanz zwischen erwartetem und tatsächlichem Ergebnis zuerst von Hand nachrechnen (hier: die Testdaten Zeile für Zeile durchgehen), statt vorschnell einen Bug im Parser zu vermuten.

---

## 4. `git commit -m` mit mehrzeiliger Nachricht scheiterte in PowerShell an Anführungszeichen im Text

**Problem:** Ein `git commit -m @'...'@`-Aufruf mit einer mehrzeiligen Commit-Message, die doppelte Anführungszeichen enthielt (z. B. wörtlich zitierte Anzeige-Ausgabe wie `"Montag 21:03..."`), scheiterte mit mehreren `error: pathspec '...' did not match any file(s)` — Git interpretierte Teile der Nachricht als zusätzliche Datei-Argumente.
**Ursache:** PowerShell reicht Strings mit eingebetteten doppelten Anführungszeichen beim Aufruf nativer Programme (wie `git.exe`) nicht immer als ein einziges Argument durch — an den Anführungszeichen wird die Zeichenkette neu tokenisiert, wodurch `-m` nur noch einen Teil der Nachricht bekommt und der Rest als lose Wörter (vermeintliche Dateipfade) ankommt.
**Lösung:** Commit-Nachricht stattdessen in eine temporäre Datei im Scratchpad-Ordner schreiben und `git commit -F <datei>` verwenden — das umgeht die Argument-Requotierung vollständig.
**Regel:** Sobald eine Commit-Message doppelte Anführungszeichen enthalten könnte (zitierte Ausgaben, Produktnamen in "Anführungszeichen"), grundsätzlich `-F <datei>` statt `-m` verwenden.

---

## 5. `esp_netif_sntp_init`/`ESP_NETIF_SNTP_DEFAULT_CONFIG` — Header- und Komponentennamen sind API-Version-abhängig

**Problem:** Vor dem ersten Build war unklar, ob `esp_netif_sntp.h` (moderne ESP-IDF-5.x-API) oder das ältere `esp_sntp.h`/`sntp_*`-API verwendet werden muss.
**Ursache:** ESP-IDF hat die SNTP-API über mehrere Versionen hinweg umgebaut; ältere Tutorials/Beispiele im Netz nutzen noch `sntp_setoperatingmode()`/`sntp_init()` direkt.
**Lösung:** Für ESP-IDF 5.5 die moderne, empfohlene API aus `esp_netif_sntp.h` verwendet (`ESP_NETIF_SNTP_DEFAULT_CONFIG`, `esp_netif_sntp_init`, `sync_cb`-Callback) — kompilierte und funktionierte im ersten Anlauf ohne Anpassung.
**Lehre:** Bei ESP-IDF-Netzwerk-APIs immer die zur installierten IDF-Version (hier: v5.5.4, siehe `ENTWICKLUNG.md`) passende Doku/Header prüfen, nicht die erstbeste Suchtreffer-Version übernehmen.

---

## 6. RGB-Display flackerte, unabhängig von jeder erkennbaren Aktion im Programm

**Problem:** Nach Einführung von WLAN/Kalender flackerte das Display gelegentlich sichtbar — zunächst jede Sekunde, später seltener, laut Nutzerbeobachtung "zwischendrin, ohne dass irgendwas passiert".
**Ursache:** Mehrschichtig. (1) `lv_label_set_text()`/`lv_obj_set_style_bg_opa()` wurden jede Sekunde aufgerufen, auch wenn sich der Wert nicht änderte — jeder Aufruf löst ein Redraw aus, beim vollflächigen Dimm-Overlay sogar einen kompletten Panel-Refresh. (2) Das RGB-Display holt seinen Framebuffer per DMA direkt aus dem PSRAM; WLAN teilt sich denselben Speicherbus — der periodische Modem-Schlaf/Aufwach-Zyklus (Power-Save) erzeugte kurze Bus-Konflikte, die sich als Flackern zeigten, unabhängig von jeglicher App-Logik.
**Lösung:** (1) Änderungs-Prüfung vor jedem `lv_label_set_text`/`lv_obj_set_style_bg_opa`-Aufruf — nur bei tatsächlich neuem Wert setzen (siehe `uhr_tick` in `app_main.c`). (2) `esp_wifi_set_ps(WIFI_PS_NONE)` nach `esp_wifi_start()` in `netz.c` — das Board hängt ohnehin am Netzteil, Stromsparen ist unnötig. (3) `bounce_buffer_size_px` in `anzeige.c` von 10 auf 20 Zeilen verdoppelt, für mehr Puffer gegen PSRAM-Zugriffsverzögerungen.
**Lehre:** Bei "flackert unregelmäßig, ohne erkennbaren Auslöser im eigenen Code" auf ESP32-S3-Boards mit RGB-Display + PSRAM-Framebuffer + WLAN zuerst an Bus-Kontention zwischen WLAN und Display-DMA denken, nicht nur an die eigene Render-Logik.

**Nachtrag (Einblend-Animation flackerte trotz allem heftig):** Vollflächige Redraws in schneller Folge (die Einblend-Animation blendet das ganze 800×480-Panel bei jedem Schritt neu durch) überforderten die PSRAM-Bandbreite bei 80 MHz grundsätzlich — da hilft keine Change-Detection, die Arbeit ist ja echt. Lösung auf `sdkconfig`-Ebene (Espressif-Empfehlung für RGB-Panels): **PSRAM auf 120 MHz** (`CONFIG_SPIRAM_SPEED_120M` + `CONFIG_IDF_EXPERIMENTAL_FEATURES`, verdoppelt die Bandbreite), dazu zwingend **Flash ebenfalls auf 120 MHz** (`CONFIG_ESPTOOLPY_FLASHFREQ_120M` — Flash und PSRAM teilen sich den MSPI-Taktgeber, sonst Build-Fehler `MSPI_TIMING_..._CORE_CLK_240M_MODULE_CLK_80M_... undeclared`), **Daten-Cache 64 KB mit 64-Byte-Zeilen** (`CONFIG_ESP32S3_DATA_CACHE_64KB`/`_LINE_64B`) und `CONFIG_LCD_RGB_RESTART_IN_VSYNC` als Selbstheilung bei Bounce-Buffer-Unterlauf. 120 MHz gilt als "experimental" (nur bis 85 °C Chiptemperatur) — für den Wohnzimmereinsatz unkritisch. Danach war das Einblenden komplett flackerfrei.

**Gemeldet (19.08.2026):** Dieser Befund ging an Waveshare (Support-Ticket 251981) — die 120-MHz-Einstellungen stehen in deren Wiki bereits, die Bus-Konkurrenz zwischen WLAN-Modem-Schlaf und Framebuffer-DMA sowie `CONFIG_LCD_RGB_RESTART_IN_VSYNC` aber nicht. Antwort vom 21.08.2026: Sie prüfen, entsprechende Hinweise aufzunehmen. Wer hier landet, sollte deshalb auch dort nachsehen — möglicherweise steht es inzwischen im Wiki.

---

## 7. Absturz mit hängendem schwarzem Bildschirm nach Kaltstart — fehlender `lvgl_port_lock()` um `lv_anim_*`/`lv_timer_create`

**Problem:** Nach dem Einbau einer Einblend-Animation (Overlay-Deckkraft per `lv_anim` über 2s) blieb das Board nach einem Kaltstart (USB ab-/wieder angesteckt) mit komplett schwarzem Bildschirm hängen.
**Ursache:** `lv_anim_init/set_var/.../lv_anim_start` sowie `lv_timer_create` wurden aus dem `main`-Task heraus aufgerufen, **ohne** sie wie jede andere LVGL-Funktion im Projekt mit `lvgl_port_lock()/unlock()` gegen den parallel laufenden LVGL-Task abzusichern. Der Task-Watchdog schlug 5 Sekunden später zu: `main` lief in eine Endlosschleife (vermutlich Korruption der internen LVGL-Animations-/Timer-Listen durch den Datenwettlauf) und ließ den Idle-Task verhungern.
**Lösung:** Den gesamten Block (`lv_anim_*` und `lv_timer_create`) in `lvgl_port_lock(0); ... lvgl_port_unlock();` gekapselt — danach lief die Sequenz reproduzierbar sauber durch (verifiziert per Phasen-Logging in `app_main()`).
**Lehre:** Ausnahmslos **jeder** Aufruf einer LVGL-Funktion außerhalb des LVGL-Tasks selbst muss gesperrt werden — auch scheinbar harmlose "nur registrieren"-Funktionen wie `lv_anim_start`/`lv_timer_create`, die selbst nicht sofort zeichnen. Ein fehlender Lock zeigt sich nicht zuverlässig als sofortiger Crash, sondern manchmal erst Sekunden später und nur bei bestimmtem Timing (hier: nur nach Kaltstart reproduzierbar, nicht bei jedem Soft-Reset) — bei "seltsamem, timingabhängigem Hänger nach LVGL-Änderungen" zuerst alle neuen LVGL-Aufrufe auf fehlende Locks prüfen.

---

## 8. Absturz `assert failed: xQueueSemaphoreTake ... uxItemSize == 0` nach Erweiterung der Kalender-Datenschicht

**Problem:** Nach Einbau strukturierter Tageseinträge (für die Tagesansicht/Touch-Abhaken, `kalender_anzeige_heutige_eintraege()` u.ä.) stürzte das Board reproduzierbar während der Kalender-Boot-Phase ab, mit `assert failed: xQueueSemaphoreTake queue.c:1713 (pxQueue->uxItemSize == 0)`.
**Ursache:** Diese Assertion feuert, wenn ein an `xSemaphoreTake`/`xSemaphoreGive` übergebenes Handle kein echtes Semaphor mehr ist — hier vermutlich Folge eines Stack-Overflows im Kalender-Task: `fuer_heute_neu_parsen()` hält jetzt gleichzeitig den bestehenden `ics_termin_t termine[32]`-Puffer (~3,8 KB), die formatierten Anzeige-Texte (`kalender_anzeige_t neu`, ~1,3 KB) UND die neuen strukturierten Einträge (`neue_eintraege[12]`, ~1,3 KB) als lokale Stack-Variablen — der bisherige 8 KB-Stack des Tasks reichte dafür nicht mehr, ein Overflow beschädigte vermutlich das (heap-allozierte) Mutex-Handle.
**Lösung:** Stack der Kalender-Task in `kalender_task_starten()` (`kalender_anzeige.c`) von 8192 auf 16384 Bytes verdoppelt.
**Lehre:** Beim Hinzufügen weiterer, nicht ganz kleiner lokaler Puffer/Structs in einer bestehenden Task-Funktion immer auch die Stack-Größe der Task gegenprüfen, nicht nur die Logik — ein Stack-Overflow zeigt sich oft nicht als offensichtlicher Absturz an der Überlaufstelle selbst, sondern als scheinbar unzusammenhängender FreeRTOS-Assert (hier: ein Mutex, keine erkennbare Verbindung zu den neuen Puffern) irgendwo später im selben Task.

---

## 9. Boot-Loop zuhause, nachdem bei den Eltern ein neues WLAN-Profil gespeichert wurde

**Problem:** Nachdem bei den Eltern deren WLAN über den Einrichtungsbildschirm gespeichert wurde, kam das Board zuhause nicht mehr ins eigene WLAN und blieb in der Boot-Loop (60s-Timeout der WLAN-Phase → Neustart → von vorn).
**Ursache:** `beste_konfiguration_ermitteln()` (`netz.c`) verglich beim Scan nur die im NVS gespeicherten Profile gegen die sichtbaren Netze. Fand sich dabei kein Treffer, fiel die Funktion **blind auf das zuletzt gespeicherte Profil** zurück — das feste Basisnetz aus `secrets.h` wurde dabei komplett übergangen, sobald überhaupt ein Profil existierte. Da zuhause nie explizit über den Einrichtungsbildschirm gespeichert wurde (es lief bisher immer über den `secrets.h`-Fallback), enthielt die Profilliste nach dem Elternbesuch nur das Eltern-WLAN — zuhause wurde also blind (und erfolglos) das Eltern-Netz probiert, obwohl das eigene Netz im Scan sichtbar war.
**Lösung:** Nach den gespeicherten Profilen wird jetzt zusätzlich `secrets.h`s SSID gegen die Scan-Ergebnisse geprüft, bevor auf das blinde Zuletzt-gespeichert-Raten zurückgefallen wird. `secrets.h` bleibt damit ein dauerhaftes Basisnetz, unabhängig davon, wie viele zusätzliche Profile später gespeichert werden.
**Lehre:** Bei einem "netter zusätzlicher Fallback" (hier: gespeicherte Profile) darauf achten, dass er einen bestehenden, bereits funktionierenden Fallback (hier: `secrets.h`) nicht *verdrängt*, sobald er zum ersten Mal greift — sonst funktioniert ein bisher zuverlässiger Pfad plötzlich in einem Fall nicht mehr, den man beim Testen des neuen Features leicht übersieht (hier: "zuhause", weil dort ja noch nie ein Profil gespeichert wurde).

---

## 10. Stack-Overflow im `main`-Task nach UI-Ueberarbeitung der Tagesansicht

**Problem:** Nach mehreren UI-Verbesserungen an der Wochentag-Navigation (aktiver Rahmen, Terminfarbe pro Button, Uebersichtstext mit gedaempften vergangenen/abgehakten Eintraegen) stuerzte das Board reproduzierbar kurz nach "Bildschirm gewechselt" beim allerersten Start ab: `***ERROR*** A stack overflow in task main has been detected.`
**Ursache:** Zwei Aenderungen trafen im selben Aufruf zusammen. (1) `uhr_tick()` (`app_main.c`) baut den Tabletten-/Termine-Uebersichtstext jetzt selbst aus den strukturierten Tageseintraegen zusammen — zusaetzliche lokale Puffer (`kalender_tag_eintrag_t[12]` + zwei 640-Byte-Textpuffer, ~2,5 KB). (2) `tagesansicht_tag_aktualisieren()` ruft jetzt fuer alle 7 Wochentag-Buttons `kalender_anzeige_eintraege_fuer_tag()` auf, um die Terminanzahl fuer die Button-Farbe zu ermitteln — dieselbe Funktion mit dem grossen `ics_termin_t[32]`-Puffer (~3,9 KB), der schon einmal den Kalender-Task-Stack sprengte (siehe Eintrag 8). Beides passiert verschachtelt innerhalb des allerersten, noch synchronen `uhr_tick(NULL)`-Aufrufs in `app_main()` — auf dem `main`-Task, dessen Standard-Stack mit nur 3584 Bytes viel kleiner ist als der eigens vergroesserte Kalender-Task-Stack. Ein erster Versuch, den Stack nur auf 8192 zu verdoppeln, reichte noch nicht.
**Loesung:** `CONFIG_ESP_MAIN_TASK_STACK_SIZE` in `sdkconfig.defaults` auf 16384 gesetzt (denselben Wert, der schon fuer den Kalender-Task noetig war) — danach lief der Boot per Serial-Log verifiziert sauber durch.
**Lehre:** Eine Funktion mit bekannt grossem Stack-Bedarf (hier: `kalender_anzeige_eintraege_fuer_tag()`, bereits in Eintrag 8 dokumentiert) bleibt ein Risiko, egal von *welchem* Task sie aufgerufen wird — beim Verdrahten in einen neuen Aufrufpfad (hier: 7× aus `uhr_tick()` heraus, noch dazu auf dem kleinen `main`-Task) immer pruefen, ob der aufrufende Task genug Stack-Headroom hat, nicht nur, ob die Funktion selbst "billig" wirkt.

---

## 11. `lv_obj_align()`/`lv_obj_get_x()` direkt nach dem Erzeugen eines Fensters lieferten falsche Koordinaten

**Problem:** Ein per `lv_obj_align(obj, LV_ALIGN_TOP_RIGHT, -Rand, y)` rechtsbuendig positionierter Schieberegler im Tabletten-Bestaetigungsfenster landete auf dem echten Geraet nicht rechtsbuendig, sondern nahe der linken Fensterkante und ueberlappte den Beschriftungstext (per Foto vom Nutzer entdeckt).
**Ursache:** Per temporaerem Debug-Log (`lv_obj_get_coords()` auf Eltern- und Kindobjekt) verifiziert: Direkt nach dem Erzeugen des Fenster-Panels (`lv_obj_create` + Groesse setzen, alles im selben Funktionsaufruf) waren dessen Koordinaten noch **nicht aufgeloest** — `lv_obj_get_coords()` lieferte einen Rahmen von `[0..-1]` (effektiv Breite 0). LVGL berechnet Kind-Positionen/-Ausrichtungen normalerweise erst beim naechsten internen Layout-Durchlauf, nicht sofort synchron beim Aufruf von `lv_obj_align`/`lv_obj_set_size`. `lv_obj_align` richtete sich dadurch faelschlich an einem Null-Rahmen aus und landete nahe x=0; das anschliessende `lv_obj_get_x()` fuer die Knopf-Positionierung las ebenfalls den (falschen) vorlaeufigen Wert.
**Loesung:** `lv_obj_update_layout(parent)` sowohl vor als auch nach `lv_obj_align()` aufgerufen — erzwingt die sofortige Aufloesung der Koordinaten, statt auf den naechsten LVGL-Zyklus zu warten.
**Lehre:** Wird ein frisch erzeugtes Objekt (insbesondere ein soeben erst angelegtes Fenster/Panel) im *selben* Aufruf per `lv_obj_align()` ausgerichtet und die aufgeloeste Position sofort per `lv_obj_get_x()`/`lv_obj_get_coords()` weiterverwendet, immer vorher `lv_obj_update_layout()` aufrufen — sonst arbeitet man mit Koordinaten von *vor* der Layout-Berechnung. Ein per Debug-Log sichtbar gemachter `[0..-1]`-Rahmen ist das eindeutige Erkennungsmerkmal dieses Problems.

## 12. Display-Rotation (180°) letztlich verworfen — `direct_mode`/Anti-Tearing und Rotation vertragen sich nicht

**Ausgangslage:** Fuer den neuen Einstellungen-Bildschirm sollte ein Schalter das Display per Software um 180° drehen (Kabelaustritt oben statt unten).

**Versuch 1 — reines `esp_lcd_panel_mirror()`:** `lv_display_set_rotation(display, LV_DISPLAY_ROTATION_180)` gesetzt, ohne weitere Flags. Ergebnis: der Touch drehte sich (per Test durch Peter bestaetigt: Antippen traf verkehrt), das angezeigte Bild blieb aber unveraendert stehen. Ursache: `esp_lvgl_port_disp.c` ruft bei gesetzter Rotation `esp_lcd_panel_mirror()` auf dem Panel-Handle auf (reines Hardware-Mirroring) — aber dieses Projekt nutzt `flags.direct_mode = true`, wobei LVGL direkt in den ohnehin schon vorhandenen Panel-Framebuffer zeichnet. Der abschliessende `esp_lcd_panel_draw_bitmap(panel, 0, 0, hres, vres, color_map)`-Aufruf bekommt dadurch als Quelle denselben Speicher, der auch das Ziel ist — ein "Spiegeln" hat nichts zum Umkehren. Der Touch drehte sich trotzdem, weil LVGL dessen Koordinaten unabhaengig vom Panel-Treiber rein softwareseitig transformiert (reine LVGL-Buchfuehrung, kein Bezug zur tatsaechlichen Bildausgabe).

**Versuch 2 — `flags.sw_rotate = true` + `flags.buff_spiram = true`:** Aktiviert bei `esp_lvgl_port` den CPU-Rotationspfad (`lv_draw_sw_rotate()` in einen separaten, aus PSRAM allozierten Zwischenpuffer `draw_buffs[2]`, bevor `esp_lcd_panel_draw_bitmap()` aufgerufen wird). Das Bild drehte sich jetzt tatsaechlich mit — aber das Display flackerte nach dem Einschalten wild (von Peter am Geraet beobachtet). Ursache per Studium von `esp_lcd_panel_rgb.c`s `rgb_panel_draw_bitmap()` gefunden: Bei `num_fbs=2` (Anti-Tearing-Doppelpufferung) erkennt der Treiber normalerweise "der LVGL-Zeichenpuffer IST einer meiner beiden eigenen Framebuffer" und schaltet nur blitzschnell (`cur_fb_index`) um — kein Kopieren, kein Reissen. `draw_buffs[2]` ist aber ein DRITTER, eigener Speicherbereich, den der Treiber nicht als "eigenen" Puffer erkennt. Er faellt dadurch auf den "echten Kopier"-Pfad zurueck, der die Daten per `memcpy` in `rgb_panel->fbs[rgb_panel->cur_fb_index]` schreibt — also GENAU in den Puffer, der gerade laufend per DMA/Bounce-Buffer an den Bildschirm gesendet wird, ohne auf den naechsten Bildwechsel zu warten. Die Kopie ueberschreibt damit fortlaufend Bilddaten, die noch ausgelesen werden.
**Rueckbau:** Peter hat sich entschieden, die Kabelfuehrung stattdessen hardwareseitig zu loesen (laengeres/gewinkeltes Kabel bzw. andere Befestigung) statt das Risiko einer tieferen Treiber-Umgehung (Rotationspuffer selbst zu einem der beiden echten Treiber-Framebuffer machen) auf einem Geraet einzugehen, das bei den Eltern zuverlaessig laufen soll. Rotation komplett wieder aus dem Code entfernt (kein totes/halb funktionierendes Feature).
**Lehre:** `lv_display_set_rotation()`/`esp_lvgl_port`s `sw_rotate` sind fuer RGB-Panels mit `direct_mode` + Anti-Tearing-Doppelpufferung (`num_fbs=2`, Bounce-Buffer) **nicht ohne Weiteres sicher nutzbar** — beide Rotationswege (Hardware-Mirror wie auch CPU-`sw_rotate`) kollidieren auf unterschiedliche Weise mit der Fast-Path-/Doppelpuffer-Logik des ESP-IDF-RGB-Treibers (`rgb_panel_draw_bitmap()`: Puffer-Identitaetspruefung per Zeigervergleich `draw_buffer >= fbs[i] && draw_buffer < fbs[i]+fb_size`). Touch-Rotation alleine ist **kein** Beleg dafuer, dass sich auch das Bild dreht — LVGL transformiert Touch-Koordinaten unabhaengig vom tatsaechlichen Rendering. Vor einem erneuten Anlauf: entweder den Rotationspuffer selbst in einen der beiden echten Treiber-Framebuffer schreiben lassen (eigener Flush-Callback noetig, umgeht `esp_lvgl_port`s eingebaute Rotation), oder das Problem komplett ausserhalb der Software loesen (wie hier: Kabel/Gehaeuse).

**Gemeldet (19.08.2026):** Auch diese Einschraenkung ging an Waveshare (Support-Ticket 251981); ihr Wiki erwaehnt Rotation und `direct_mode` bisher mit keinem Wort. Antwort vom 21.08.2026: Sie pruefen eine Ergaenzung der Dokumentation.

## 13. Anzeige fiel waehrend der Tabletten-Bedienung in den Nacht-Modus zurueck — LVGL-Presses bubbeln nicht automatisch zum Screen

**Problem:** Peter berichtete nach einem laengeren Praxis-Test, dass die Anzeige beim Bedienen des Heute-Fensters (Tabletten abhaken) mitten in der Interaktion in den dunklen Abend-/Nacht-Modus zurueckfiel, obwohl die Beruehrungs-Wachzeit (`BERUEHRUNG_WACHZEIT_US`, 30s) eigentlich genau das verhindern soll.
**Ursache:** `beruehrung_callback` haengt nur an `s_bildschirm` selbst (`LV_EVENT_PRESSED`). Die Tages-/Heute-Fenster von `tagesansicht.c` sind aber eigene, klickbare `lv_obj_create(s_scr)`-Panels, die als Overlay ÜBER dem Hauptbildschirm sitzen. LVGL leitet `LV_EVENT_PRESSED` standardmaessig NICHT automatisch an Vorfahren-Objekte weiter (kein Bubbling ohne `LV_OBJ_FLAG_EVENT_BUBBLE` auf dem jeweiligen Kind) - jeder Tipp auf einen Schieberegler, den Schliessen-Button oder auch nur den Fensterhintergrund wird also vom Fenster-Panel selbst "geschluckt" und erreicht `s_bildschirm`s Callback nie. Sobald ein solches Fenster offen ist, verlaengert keine Interaktion darin mehr die 30s-Wachzeit.
**Loesung:** `aktueller_modus()` (app_main.c) prueft zusaetzlich `tagesansicht_fenster_offen()` (neue Abfrage in tagesansicht.c, `true` solange `s_tages_fenster`/`s_heute_fenster` existieren) und erzwingt Tag-Modus, solange irgendein Tages-/Heute-Fenster offen ist - unabhaengig vom Ablauf der 30s-Wachzeit.
**Lehre:** Ein `LV_EVENT_PRESSED`-Handler auf einem Screen/Root-Objekt faengt NUR Presses auf nicht-klickbare Kinder ab (z. B. reine `lv_label`s, bei denen der Hit-Test bis zum naechsten klickbaren Vorfahren "durchfaellt"). Sobald ein klickbares Overlay (Fenster, Popup, Keyboard, ...) darueber liegt, sieht der Root-Handler von Interaktionen darin nichts mehr - fuer sowas immer eine explizite Zustandsabfrage ("ist gerade ein Overlay offen?") statt sich auf Event-Bubbling zu verlassen.

## 14. WLAN-Watchdog und Boot-Timeout konnten in eine nie endende Neustart-Schleife fuehren

**Problem:** Peter fragte nach, ob ein Verbindungsverlust wirklich sofort einen Neustart ausloest (siehe #9/Watchdog). Beim genaueren Durchdenken kam eine zweite, eigentlich noch gefaehrlichere Stelle zum Vorschein: der Boot-Vorgang selbst hat pro Phase (WLAN/Zeit/Kalender) einen eigenen 60s-Countdown, der bei Ablauf bisher `esp_restart()` ausloeste (`phase_fehlgeschlagen_neustart`). Ohne batteriegepufferte RTC verliert das Geraet bei JEDEM Neustart die Uhrzeit komplett - und die Software kann einen echten Stromausfall nicht von einer (nur zeitweise) fehlenden WLAN-Verbindung unterscheiden. Bleibt WLAN laenger weg (schwaches Signal, Router-Ausfall), waere das Geraet in eine echte Endlosschleife gelaufen: Boot → 60s auf WLAN warten → Timeout → Neustart → Boot → wieder 60s warten → ... fuer immer, OHNE dass die Hauptanzeige (Uhrzeit/Tabletten/Termine) je erscheint. Peters explizite Prioritaet: die Anzeige muss laufen, notfalls mit veralteter Zeit - eine nicht angezeigte faellige Tablette waere der eigentliche Super-GAU.
**Loesung (zweiteilig):**
1. **Watchdog gelockert statt deaktiviert:** `netz_watchdog_lockern()` (netz.c) wird von `app_main()` genau einmal aufgerufen, sobald WLAN/Zeit/Kalender erfolgreich durchgelaufen sind und die Hauptanzeige gleich erscheint. Davor gilt weiter die scharfe 30s-Grenze (kurze Aussetzer beim Booten sollen weiter schnell zu einem sauberen Neustart fuehren), danach eine Woche - ein Neustart ist ab da nur noch ein allerletzter Reparaturversuch.
2. **Boot-Timeout macht automatisch weiter statt neu zu starten:** `phase_timeout_automatisch_fortsetzen()` ersetzt `phase_fehlgeschlagen_neustart()` - laeuft eine Boot-Phase in den 60s-Timeout, wird sie trotzdem als erledigt markiert (wie beim manuellen "Offline"-Button, nur ohne dass jemand danebenstehen muss). Ist die Zeit noch nicht bekannt, wird sie per neuer Funktion `zeit_uebernehmen()` (zeit.c) auf den zuletzt angezeigten Zeitstempel gesetzt (`einstellungen_letzte_anzeige()`) - genau wie bei manueller Eingabe wird das als unbestaetigt markiert (`zeit_ist_manuell_gesetzt()`). Neu: die grosse Uhrzeit blinkt jetzt zusaetzlich dunkelorange (`FARBE_ZEIT_UNBESTAETIGT`, uhr_tick()), solange die Zeit unbestaetigt ist - deutlich auffaelliger als nur das kleine durchgestrichene Status-Symbol.
**Verifiziert:** Testweise `WLAN_SSID` in secrets.h auf eine garantiert nicht vorhandene SSID gesetzt, geflasht, per Boot-Log den kompletten 60s-Timeout abgewartet: Phase wurde nach 60s automatisch als erledigt markiert (kein Neustart), Uhrzeit vom letzten bekannten Stand uebernommen, Kalender aus dem Cache geladen, Hauptanzeige erschien regulaer, Watchdog auf 1 Woche gelockert - danach ueber 15s weitere WLAN-Fehlversuche im Hintergrund beobachtet, ohne dass irgendetwas neu startete. Anschliessend `WLAN_SSID` zurueckgesetzt und normalen Boot (mit echtem WLAN) erneut verifiziert.
**Nachtrag (17.07.2026):** Peter wies darauf hin, dass der uebernommene "letzte bekannte Zeitstand" selbst schon veraltet ist - die Boot-/Warte-Zeit bis zu diesem Punkt (inkl. eines evtl. schon durchlaufenen 60s-Timeouts einer frueheren Phase) verstreicht ja zusaetzlich. `phase_timeout_automatisch_fortsetzen()` addiert deshalb jetzt `esp_timer_get_time()/1000000` (Sekunden seit dem Einschalten, laeuft unabhaengig von der Anzahl durchlaufener Phasen-Timeouts mit) auf `einstellungen_letzte_anzeige()`, bevor `zeit_uebernehmen()` aufgerufen wird - die geschaetzte Zeit liegt damit deutlich naeher an der Wahrheit als der reine, unveraenderte alte Zeitstempel.

---

## 15. Tabletten-Bestaetigungsstatus ueberlebte keinen Neustart

**Problem:** Peter merkte an, dass eine bereits als "genommen" abgehakte Tablette nach einem Neustart wieder als faellig erscheinen wuerde, da `s_heute_eintraege[].bestaetigt` (kalender_anzeige.c) nur im RAM lebt. Zwar startet das Geraet nach dem Fix in #14 nicht mehr reflexartig bei WLAN-Problemen neu, ein Neustart durch Stromausfall/Panic bleibt aber weiterhin moeglich - und wuerde dann faelschlich zu einer doppelten Einnahme fuehren koennen.
**Ueberlegt, aber verworfen:** Bestaetigung zurueck in den Google-Kalender schreiben. Der genutzte "private ICS-Adresse"-Link ist bei Google grundsaetzlich nur lesend - ein Zurueckschreiben braeuchte die vollstaendige Calendar API samt OAuth2, Token-Speicherung und -Erneuerung: unverhaeltnismaessig viel neue Angriffsflaeche/Fehlerquelle fuer dieses Projekt.
**Loesung:** Persistierung lokal auf der bereits gemounteten Cache-Partition (`kalender_speicher.c`, dieselbe wear-levelled FAT-Partition wie der ICS-Cache). Neue Funktionen `kalender_speicher_bestaetigungen_schreiben()`/`_lesen()` speichern eine kleine Textdatei: erste Zeile der Tages-Schluessel (JJJJMMTT), danach ein Titel pro Zeile. `kalender_anzeige_tablette_bestaetigen()` schreibt bei jeder Aenderung den kompletten aktuellen Bestaetigt-Stand neu. `fuer_heute_neu_parsen()` wendet die gespeicherten Titel nur beim allerersten Parse-Durchlauf seit dem Start an (`s_letzter_tag_schluessel == -1`) UND nur, wenn der gespeicherte Tages-Schluessel exakt zu heute passt (sonst Bestaetigungen vom Vortag - werden verworfen, genau wie beim normalen Mitternachts-Reset).
**Fallstrick dabei (Format-Truncation):** `snprintf(ziel, ICS_TITEL_MAX, "%s", eintrag.titel)` loeste erneut `-Werror=format-truncation` aus (derselbe Effekt wie in `eintrag_uebernehmen()`, siehe Kommentar dort) - Array-Zugriff via Zeiger/Index verliert fuer GCCs Truncation-Pruefung die bekannte Zielgroesse. Behoben mit derselben `"%.*s"`-Praezisions-Notation.
**Zweiter, echter Fallstrick (nach erstem Live-Test):** Peter meldete "Haken bleibt nicht erhalten" - Diagnose-Logging (errno) zeigte `fopen(".../tabletten.txt", "wb")` schlug mit `errno=22 (EINVAL)` fehl, die Datei wurde also nie geschrieben. Ursache: das Projekt nutzt `CONFIG_FATFS_LFN_NONE` (keine langen Dateinamen, spart RAM) - FatFs erlaubt dann nur klassische 8.3-Kurznamen (max. 8 Zeichen Basisname). `tabletten.txt` hat 9 Zeichen ("tabletten") und wird als ungueltiger Name abgelehnt; `kalender.ics` (Basisname genau 8 Zeichen) funktionierte deshalb zufaellig schon immer. Behoben durch Umbenennen auf `tablette.txt` (8 Zeichen, passt exakt). **Lehre:** bei `CONFIG_FATFS_LFN_NONE` IMMER pruefen, ob ein neuer Dateiname ins 8.3-Schema passt - der Fehler zeigt sich erst zur Laufzeit (`fopen` liefert NULL) und nicht beim Kompilieren.
**Verifiziert:** Erst mit dem defekten Namen live getestet (Peter: Haken weg nach Neustart) → errno-Log bestaetigte die Ursache → nach Umbenennung erneuter Live-Test (Tablette abhaken, Board neu starten): Boot-Log zeigt `Bestaetigungen gespeichert: Tag=20260717, 1 Titel` vor dem Neustart und `Bestaetigungen gelesen: Tag=20260717, 1 Titel` / `1 bereits bestaetigte Tablette(n) von Flash uebernommen` danach: Peter bestaetigte, dass der Haken am Bildschirm tatsaechlich erhalten blieb.
**Lehre:** Bei einem Geraet ohne batteriegepufferte RTC ist jeder automatische Neustart-auf-Fehler-Reflex mit Vorsicht zu geniessen, sobald ein Fehlerzustand laenger anhalten koennte, als der Neustart selbst kostet - die Software kann "kurzer Aussetzer" nicht von "dauerhafter Ausfall" unterscheiden, und ein Neustart-Loop ohne Fluchtweg ist immer schlimmer als ein degradierter, aber laufender Zustand. Timeout-Handler sollten deshalb einen Ausweg mit reduzierter Funktionalitaet anbieten (hier: letzter bekannter Zeitstempel + Cache-Daten + deutliche Unbestaetigt-Kennzeichnung), statt bedingungslos neu zu starten.

---

## 16. Geraet fror nach mehreren Einstellungen-Runden komplett ein — LVGL-Pool lief durch geleakte Menue-Screens voll

**Symptome (verwirrend vielfaeltig):** Nach dem Einbau von WLAN-Scan-Dropdown und RSSI-Anzeige fror das Geraet wiederholt komplett ein (Task-Watchdog meldete verhungernde IDLE0-Task, LVGL-Task zeichnete pausenlos). Die Haenger wirkten zufaellig: mal im WLAN-Bildschirm, mal beim Wechsel zur Hauptanzeige — aber immer erst NACH mehreren Bedien-Runden im Einstellungen-Menue. Zuvor zeigte die Dropdown-Liste zeitweise wirre Zeichen.

**Falsche Faehrten (der Reihe nach verfolgt und verworfen):**
1. *Kaputte Nachbar-SSID als Ausloeser:* WLAN-SSIDs sind rohe Bytes und duerfen ungueltiges UTF-8 enthalten — als Anzeige-Haertung wurden alle SSIDs vor der Uebernahme in LVGL-Widgets bereinigt (`ssid_anzeige_bereinigen`, Zeichen ausserhalb unseres Font-Umfangs → "?"). Sinnvoll, aber nicht die Haenger-Ursache.
2. *Stack-Overflow der Event-Task:* `wifi_ap_record_t aps[16]` (>1,3 KB) lag als lokales Array auf dem nur 2304 Byte grossen sys_evt-Stack — real und behoben (static), erklaerte einen fruehen Absturz, aber nicht die spaeteren.
3. *Use-after-free im LVGL-Dropdown:* `lv_dropdown_set_options()` gibt den Options-Puffer frei, auf den das Label der GEOEFFNETEN Liste noch per `lv_label_set_text_static()` zeigt (lv_dropdown.c/lv_dropdown_open) — erklaert die wirren Zeichen. Behoben: offene Liste vor dem Optionen-Tausch schliessen. Echter LVGL-Fallstrick, aber ebenfalls nicht die Haenger-Ursache.
4. *Heap-Korruption:* Comprehensive-Heap-Poisoning + periodische Integritaetspruefung fanden NICHTS — wichtiger Negativbefund: kein wildes Ueberschreiben.

**Durchbruch per Core-Dump statt Log-Raten:** `CONFIG_ESP_TASK_WDT_PANIC=y` + `CONFIG_ESP_COREDUMP_ENABLE_TO_UART=y` verwandeln den Haenger in eine Panic mit komplettem Speicherabzug ueber die serielle Leitung (`espcoredump.py info_corefile -t b64 ...`, fuer Variablen-Inspektion `-s core.elf` + `xtensa-esp32s3-elf-gdb`). Ergebnis: `lv_timer_handler()` kehrte minutenlang nicht zurueck (Stack-Variable `handler_start` lag 6s vor dem Absturz), das gerade gezeichnete Label war voellig gesund (13 Zeichen, Iteration bei Buchstabe 10), und der Program Counter stand in `lv_draw_buf_create_ex`/`LV_ASSERT_MALLOC` — einer SPEICHER-ALLOKATION.

**Echte Ursache:** `einstellungen_bildschirm_verarbeiten()` (app_main.c) ruft bei jeder Rueckkehr aus einem Unter-Bildschirm (WLAN/Datum/Kalender-URL) `einrichtung_einstellungen_zeigen()` erneut auf — die Funktion ueberschrieb `s_einstellungen_screen` mit einem NEUEN Screen, ohne den alten zu loeschen. Jede Menue-Runde leakte so einen kompletten Screen (~5-10 KB Widgets) in den nur `CONFIG_LV_MEM_SIZE_KILOBYTES=64` grossen LVGL-Pool (`CONFIG_LV_MEM_POOL_EXPAND_SIZE_KILOBYTES=0` — keine Erweiterung). Nach einer Handvoll Runden war der Pool erschoepft; die Glyph-Puffer-Allokation beim Zeichnen schlug fehl und die Draw-Dispatch-Schleife kam nie mehr zum Ende — als Endlos-Zeichnen sichtbar, das je nach Fuellstand an unterschiedlichen Stellen (WLAN-Screen, Menue, Hauptanzeige) zuschlug. Deshalb die scheinbar zufaelligen, immer erst "nach einer Weile" auftretenden Haenger.

**Loesung:**
1. `einrichtung_einstellungen_zeigen()` loescht einen evtl. vorhandenen alten Menue-Screen, bevor der neue gebaut wird (gefahrlos: der alte ist in dem Moment nie der aktive Screen — Regel aus #7 bleibt gewahrt).
2. Vorsorglich entschaerft (beteiligt an der Dauerlast): RSSI-Anzeige ohne Balken-Animation und nur bei tatsaechlicher Wertaenderung (2x/s statt 5 Hz mit `LV_ANIM_ON`); RSSI- und Scan-Timer werden EINMAL erzeugt und nur noch pausiert/fortgesetzt statt bei jedem Bildschirm-Uebergang geloescht/neu angelegt (jede Timer-Aenderung startet die Runde in `lv_timer_handler` von vorn).
3. `CONFIG_ESP_TASK_WDT_PANIC` + `CONFIG_ESP_COREDUMP_ENABLE_TO_UART` bleiben DAUERHAFT aktiv (sdkconfig.defaults): ein kuenftiger Haenger heilt sich nach 5s per Neustart selbst (Anzeige hat Prioritaet, siehe #14) und hinterlaesst einen Core-Dump im Log.

**Verifiziert:** Vor dem Fix reproduzierbar nach ~6-10 Menue-Runden (drei unabhaengige Core-Dumps, alle mit demselben Muster); nach dem Fix ueberstand das Geraet 10+ intensive Menue-Runden inkl. Dropdown-Bedienung und Hauptanzeige-Interaktion ohne ein einziges Watchdog-Ereignis.

**Lehren:**
- Bei "Screen neu aufbauen statt zurueckkehren"-Navigationsmustern IMMER pruefen, wer den vorherigen Screen loescht — ein Leak faellt bei 64 KB Pool erst nach mehreren Runden auf und aeussert sich dann NICHT als Fehlermeldung, sondern als eingefrorenes Geraet an scheinbar zufaelliger Stelle.
- LVGLs OOM-Verhalten ist kein sauberer Absturz: fehlgeschlagene Draw-Allokationen koennen in Endlosschleifen der Render-Pipeline enden.
- Task-WDT-Panic + UART-Core-Dump ist fuer schwer reproduzierbare Haenger um Groessenordnungen ergiebiger als jede Log-Instrumentierung — direkt zu Beginn aktivieren, nicht erst nach mehreren Raterunden.

## 17. Wirre Zeichen im Tabletten-Label nach dem Zurueckschieben des Schiebers

**Problem:** Tabletten-Schieber im "Heute"-Fenster auf "genommen" und direkt wieder zurueck — die Zeile zeigte danach Muell wie `'_?0 Frueh` statt `06:30 Frueh`.

**Ursache:** `tabletten_zeile_aktualisieren()` (tagesansicht.c) entfernte das `[x] `-Praefix per `lv_label_set_text(label, aktuell + praefix_laenge)`, wobei `aktuell` aus `lv_label_get_text(label)` stammt — also ein Zeiger IN DEN EIGENEN Textpuffer des Labels. `lv_label_set_text` realloziert zuerst genau diesen Puffer und kopiert DANACH aus der Quelle; nach dem realloc zeigt die Quelle auf freigegebenen/verschobenen Speicher. Der Einschalt-Pfad (Praefix ergaenzen) war korrekt, weil er zuerst in einen lokalen Puffer formatierte — deshalb trat der Fehler nur beim Entfernen auf.

**Loesung:** Vor dem `lv_label_set_text` immer erst in einen lokalen Puffer kopieren, wenn die Quelle aus demselben Widget stammt.

**Lehre:** Gleiche Fehlerklasse wie der Dropdown-Fallstrick aus #16 (Punkt 3): LVGL-Setter duerfen NIE mit Zeigern gefuettert werden, die in den internen Puffer desselben Widgets zeigen — die Setter bauen diese Puffer waehrend des Aufrufs um. Bei jedem `lv_..._set_...(widget, quelle)` fragen: Wo zeigt `quelle` hin?

## 18. Stack-Overflow im neuen Web-Konfigurationsserver (httpd) beim ersten Seitenaufruf

**Problem:** Der neue `esp_http_server`-basierte Webserver (webkonfig.c, Kalender-Adresse per Browser aendern) stuerzte beim allerersten `GET /`-Aufruf sofort ab: `***ERROR*** A stack overflow in task ... has been detected`, danach Neustart (Panic).

**Ursache:** `start_get_handler()` legt mehrere lokale Puffer an (URL, HTML-escapte Kopie, komplette Seite - zusammen ca. 4,3 KB), auf dem `httpd`-Standard-Task-Stack von nur 4096 Byte (`HTTPD_DEFAULT_CONFIG()`). Allein diese lokalen Variablen fuellten den Stack schon fast komplett, dazu kommt httpd's eigener Verbrauch fuers Parsen der HTTP-Anfrage obendrauf.

**Loesung:** `httpd_config_t.stack_size` beim Serverstart auf 8192 gesetzt.

**Lehre:** Dieselbe Fehlerklasse wie der WLAN-Scan-Stack-Overflow in netz.c (siehe #10 bzw. die Kommentare dort zu `wifi_ap_record_t aps[16]`): Jeder neue Task/Handler mit groesseren lokalen Puffern (String-Aufbau, Formularverarbeitung, HTML-Erzeugung o.ae.) braucht eine bewusste Ueberpruefung der Stackgroesse, nicht nur den jeweiligen Default. Vor dem ersten Live-Test kurz ueberschlagen: Summe der groessten lokalen Arrays vs. konfigurierte Stackgroesse.

## 19. Bildschirmfoto-Entwicklungswerkzeug: HTTP-Weg verworfen, serieller Weg mit zwei weiteren Fallstricken

**Ausgangslage:** Fuer die Projekt-Doku sollten sich Screenshots einzelner Bildschirme (Hauptanzeige, WLAN, Einstellungen, ...) ziehen lassen. Erster Versuch: `/screenshot.bmp`-Endpunkt im Web-Konfigurationsserver (siehe #18). Nach Beheben des dortigen Stack-Overflows blieb ein zweites, grundlegenderes Problem: die Uebertragung des ca. 1,1-MB-BMPs ueber WLAN war mit ~1,8 KB/s so langsam, dass ein Bild ueber 10 Minuten gebraucht haette (mit 60s-Socket-Timeout ueber curl verifiziert: 161 KB in 90s, danach abgebrochen). Vermutete Ursache: PSRAM-Bus-Konkurrenz zwischen dem Auslesen des Screenshot-Puffers und dem WLAN-Treiber-DMA (vgl. die schon dokumentierte Flacker-Ursache in sdkconfig.defaults). Da das Feature ohnehin nur waehrend der Entwicklung gebraucht wird (nicht zur Laufzeit beim Endnutzer), wurde der HTTP-Ansatz komplett verworfen zugunsten eines Buttons auf `lv_layer_top()` (liegt automatisch ueber jedem Bildschirm), der ein Base64-BMP ueber die ohnehin fuers Flashen offene serielle USB-Verbindung ausgibt - kein WLAN, keine PSRAM/WLAN-Konkurrenz.

**Fallstrick A - Task-Watchdog durch synchrone Arbeit im LVGL-Event-Callback:** Der erste Versuch rief die komplette Aufnahme+Ausgabe direkt im Button-Klick-Callback auf - der laeuft auf dem von `esp_lvgl_port` ueberwachten LVGL-Task. Rund 12.800 einzelne Log-Zeilen bei 115200 Baud brauchen ueber zwei Minuten; das sprengte das 5s-Watchdog-Fenster bei weitem, live als sofortiger Neustart bei jedem Antippen beobachtet. **Loesung:** Die eigentliche Arbeit in einen per `xTaskCreate()` frisch erzeugten (nicht beim Watchdog angemeldeten) Task ausgelagert; der Klick-Callback selbst blendet nur den Button aus und startet den Task.

**Fallstrick B - Verhungernder IDLE-Task trotz eigenem Task:** Auch nach Fallstrick A gab es noch einen Neustart mitten in der Uebertragung. Ursache: die Sende-Schleife (Base64-Zeilen per `esp_log_write`) lief ohne jeden `vTaskDelay()` ueber die volle ~2 Minuten am Stueck durch - der eigene Task ist zwar selbst nicht beim Watchdog angemeldet, blockierte damit aber durchgehend die CPU und liess den (standardmaessig ebenfalls ueberwachten) IDLE-Task nie zum Zuge kommen. **Loesung:** Alle 20 Zeilen ein kurzer `vTaskDelay(pdMS_TO_TICKS(2))`.

**Nebenwirkung von Fallstrick B's Loesung:** Die Pausen geben anderen Tasks Gelegenheit, eigene Log-Zeilen dazwischenzuschreiben (z. B. periodische Timer-Meldungen) - im seriellen Mitschnitt erscheinen dann vereinzelte "krumme" Zeilen mitten im Base64-Block. Statt das im Geraet zu verhindern, wurde das Empfangs-/Dekodier-Skript robust dagegen gemacht: nur Zeilen behalten, die KOMPLETT aus gueltigen Base64-Zeichen bestehen (eine fremde Log-Zeile enthaelt so gut wie immer Leerzeichen/Klammern und faellt dadurch als ganze Zeile raus).

**Fallstrick C - Doppelbild durch nicht genullten PSRAM-Puffer:** Erster erfolgreicher Screenshot zeigte ein deutliches "Geisterbild" (Text/Buttons doppelt, farblich verschoben). Ursache: `lv_snapshot_take_to_draw_buf()` loescht den Zielpuffer nur, wenn intern KEIN "top object" gefunden wird (siehe `lv_snapshot.c`) - sonst bleibt der Inhalt des frisch (nicht genullten) `heap_caps_malloc()`-Puffers stehen und schimmert durch kantengeglaettete/halbtransparente Bereiche (grosse Schrift) durch. **Loesung:** Puffer vor dem Snapshot-Aufruf explizit per `memset()` auf 0 gesetzt.

**Lehren:**
- Ein Feature, das nur waehrend der Entwicklung gebraucht wird, muss nicht denselben Uebertragungsweg nutzen wie ein Laufzeit-Feature - der Wechsel von WLAN/HTTP auf die ohnehin offene serielle Verbindung war der eigentliche Durchbruch, nicht eine weitere Optimierung des HTTP-Wegs.
- "Eigener Task = nicht beim Watchdog angemeldet" loest nur die HALBE Watchdog-Problematik - ein dauerhaft CPU-blockierender Task ohne jeden Yield kann trotzdem ANDERE ueberwachte Tasks (allen voran IDLE) verhungern lassen und damit denselben Neustart ausloesen.
- LVGL-Puffer, die nicht ueber die eingebauten `lv_draw_buf_create*()`-Funktionen sondern manuell (z. B. per `heap_caps_malloc()`) gebaut werden, muessen selbst fuer eine saubere Ausgangslage sorgen (Nullung) - LVGLs eigene Vorbedingungen dafuer sind nicht immer erfuellt.

**Fallstrick D - Wochentag-Buttons an falscher Position (Nachtrag):** Auch nach Fallstrick C blieb ein Fehler: im Screenshot erschienen einige der sieben Wochentag-Buttons (z. B. Fr/Sa/So) am RECHTEN statt am linken Bildschirmrand, obwohl `tagesansicht.c` ALLE sieben Buttons nachweislich auf dieselbe feste x-Position setzt (`lv_obj_set_pos(btn, SPALTE_X, y)`, nur y aendert sich) - per Quellcode-Vergleich zweifelsfrei als echter Fehler bestaetigt, nicht etwa ein falsches Erinnern des tatsaechlichen Layouts. **Ursache:** `lv_snapshot_take_to_draw_buf()` versucht intern per `lv_refr_get_top_obj()` eine Teil-Optimierung ("nur das Noetigste neu zeichnen") - sie geht davon aus, dass der Zielpuffer schon den vorherigen Frame enthaelt (wie beim echten Bildschirm-Refresh), findet dabei ein vermeintlich "deckendes" Objekt und zeichnet ab dort nur noch dessen SPAETERE Geschwister-Objekte neu, ueberspringt aber FRUEHERE Geschwister komplett in der Annahme, deren Inhalt sei schon vorhanden. Bei einem frisch allozierten, leeren Snapshot-Puffer ist diese Annahme falsch. **Loesung:** `lv_snapshot_take_to_draw_buf()` durch eine eigene Funktion ersetzt, die IMMER den sicheren "vollstaendiges Neuzeichnen"-Zweig dieser Originalfunktion repliziert (`lv_obj_redraw()` direkt aufgerufen, unter Umgehung von `lv_refr_get_top_obj()`) - dafuer werden zwei private LVGL-Header eingebunden (`core/lv_refr_private.h`, `display/lv_display_private.h`), vertretbar fuer dieses reine Entwicklungswerkzeug. Live verifiziert: alle sieben Wochentag-Buttons stehen danach korrekt in einer Spalte am linken Rand.

**Lehre (Ergaenzung):** `lv_snapshot_take_to_draw_buf()` ist NICHT fuer beliebige Ad-hoc-Schnappschuesse in frische, leere Puffer gedacht - die interne "Teil-Redraw"-Optimierung setzt (wie ein normaler Bildschirm-Refresh) einen bereits gueltigen Vorzustand voraus. Fuer garantiert korrekte Ergebnisse in einem selbst erzeugten Puffer den sicheren Vollstaendig-Zweig direkt nachbauen, statt der bequemeren High-Level-Funktion zu vertrauen.

**Fallstrick E - Gesamtes Bild um 121px verschoben, wenn der Screen nicht bei (0,0) sitzt (Nachtrag):** Ein Screenshot des Startbildschirms, WAEHREND des Bootens aufgenommen, zeigte ALLE Elemente (die drei Status-Symbole UND das ganz anders positionierte Zahnrad unten rechts) um exakt denselben Betrag (121px) nach links verschoben - ein uniformer Versatz, kein Teil-Redraw-Problem wie bei Fallstrick D. Auf dem echten Display war alles korrekt (Peter bestaetigt), nur im Screenshot. **Ursache:** `screenshot_vollstaendig_rendern()` setzte die `buf_area`/`_clip_area` des Render-Layers hart auf `{0,0,w-1,h-1}`, statt sie - wie es `lv_snapshot.c` tut - an den TATSAECHLICHEN Koordinaten des Screens (`lv_obj_get_coords`) zu verankern. Solange der Screen exakt bei (0,0) sitzt (Normalfall im Ruhezustand, z. B. der bereits korrekte Hauptanzeige-Screenshot), ist beides identisch. Sitzt der Screen aber voruebergehend verschoben (der Startbildschirm stand beim Booten offenbar transient um 121px versetzt), zeichnet die {0,0}-Variante alle Kinder um genau diesen Versatz daneben - das echte Display-Refresh rechnet den Screen-Offset dagegen korrekt ein, daher nur im Screenshot sichtbar. **Loesung:** `buf_area`/`_clip_area` an `lv_obj_get_coords(screen)` ausrichten (buf_area.x1/y1 = Screen-Ecke, x2/y2 entsprechend + Puffergroesse). Danach landen die Kinder immer relativ zum Screen-Ursprung korrekt im Bild, egal wo der Screen gerade sitzt. Live verifiziert: nach dem Fix stehen alle Elemente exakt an ihren Soll-Positionen (200/400/600/745 statt 79/279/479/624).

**Lehre (Ergaenzung 2):** Beim Nachbauen eines LVGL-internen Render-Pfads (hier der Snapshot) NICHT vereinfachend annehmen, das Objekt sitze bei (0,0) - die Original-Funktion holt bewusst `lv_obj_get_coords()` und verankert den Puffer daran. Solche "harmlosen" Vereinfachungen fallen erst in einem transienten Zustand auf (hier: waehrend des Bootens), den man beim ersten Testen (ruhende Hauptanzeige) gar nicht trifft.

## 20. Kalender-Sync dauerhaft tot: mbedTLS-Puffer im knappen internen SRAM statt PSRAM

**Problem:** Peter bemerkte anhand eines Doku-Screenshots, dass das Kalender-Symbol rechts oben dauerhaft durchgestrichen war - kein einziger Kalender-Download gelang mehr, seit mehreren Features (Web-Konfigurationsserver, mDNS, WLAN-Laufbetrieb-Neuscan-Task, Screenshot-Werkzeug) hinzugekommen waren.

**Ursache:** Per Live-Log (`idf.py monitor`) sofort sichtbar: `mbedtls_ssl_setup returned -0x7F00` (`MBEDTLS_ERR_SSL_ALLOC_FAILED`) bei JEDEM Download-Versuch. `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC` (ESP-IDF-Standardeinstellung) zwingt mbedTLS, seine SSL-Ein-/Ausgabepuffer (`CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384` + `OUT=4096`, zusammen >20 KB) ausschliesslich aus dem internen SRAM zu holen (`heap_caps_calloc(..., MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)`, siehe `components/mbedtls/port/esp_mem.c`) - obwohl 8 MB PSRAM fast ungenutzt zur Verfuegung stehen. Der interne Speicher (nur ca. 200 KB) reichte nach Hinzunahme mehrerer neuer Tasks/Puffer (Web-Server-Stack 8 KB, mDNS-Task, WLAN-Neuscan-Task 4 KB, Kalender-Task-Stack 16 KB, ...) nicht mehr fuer die grosse zusammenhaengende mbedTLS-Allokation.

**Loesung:** In `sdkconfig.defaults` (und passend in `sdkconfig`) `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` statt `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC` gesetzt - verschiebt dieselben Puffer per `MALLOC_CAP_SPIRAM` ins PSRAM. Live verifiziert: Download gelang danach zuverlaessig (ein paar vereinzelte `MBEDTLS_ERR_SSL_CONN_EOF`-Fehlschlaege bei schwachem Signal blieben, heilen sich aber ueber die bestehende 30s-Wiederholung von selbst - das ist das schon laenger bekannte, unkritische Problem aus FAHRPLAN.md).

**Lehre:** Bei einem ESP32 mit PSRAM gilt dieselbe Grundregel wie schon fuer den Screenshot-Puffer (#19) oder LVGLs `lv_malloc()`-Pool: grosse, selten aber "auf einen Schlag" gebrauchte Puffer (TLS-Handshake, Snapshot, HTTP-Bodies) gehoeren ins PSRAM, der knappe interne SRAM bleibt sonst schnell die tatsaechliche Obergrenze fuer gleichzeitig laufende Features - auch wenn jedes einzelne Feature fuer sich harmlos wirkt. Ein Fehler, der bei JEDEM Versuch identisch auftritt (nicht nur gelegentlich), deutet eher auf eine strukturelle Ressourcengrenze hin als auf ein Netzwerk-/Signalproblem - ein Blick ins Log klaert das in Sekunden, bevor man in die falsche Richtung (WLAN-Signal, Server, URL) weitersucht.

## 21. WLAN-Netzwerksuche fand beim Aufstellen bei den Eltern keine einzige SSID

**Problem:** Beim Aufstellen des Geraets bei den Eltern blieb die Netzwerkliste
im WLAN-Einrichtungsbildschirm dauerhaft leer ("Suche Netzwerke...") - keine
einzige SSID, auch keine Nachbarnetze. Bei allen Tests zu Hause hatte dieselbe
Suche immer zuverlaessig funktioniert.

**Ursache:** Der Reconnect-Kreislauf blockierte jeden Scan. Ist kein gespeichertes
Netz in Reichweite (genau die Situation beim ersten Aufstellen an einem neuen Ort),
startet der `WIFI_EVENT_STA_DISCONNECTED`-Handler in `netz.c` nach jedem
fehlgeschlagenen Verbindungsversuch sofort bedingungslos den naechsten
`esp_wifi_connect()` - das Funkmodul steckt damit praktisch DAUERHAFT in einem
Verbindungsversuch (live gemessen: alle 2,4s ein neuer). Waehrend eines laufenden
Verbindungsaufbaus schlaegt `esp_wifi_scan_start()` aber grundsaetzlich mit
`ESP_ERR_WIFI_STATE` fehl - jeder der alle 2s wiederholten Scans des
Einrichtungsbildschirms scheiterte sofort, die Liste blieb leer. Zu Hause konnte
das nie auffallen: dort war immer ein gespeichertes Netz sichtbar, das Geraet also
verbunden, und ein verbundenes Funkmodul darf scannen. Der Effekt war sogar schon
einmal halb entdeckt worden: ein Kommentar in `wlan_scan_tick_cb` (einrichtung.c)
erwaehnte das Scan-Scheitern bei parallelem Verbindungsaufbau explizit - behandelt
wurde damals aber nur der Log-Spam (2s-Drossel), nicht die Ursache.

**Loesung:** Neue Funktion `netz_verbindungsversuche_pausieren(bool)` in netz.c:
`einrichtung_wlan_zeigen()` haelt damit den Reconnect-Kreislauf an (ein gerade
laufender Verbindungsversuch wird per `esp_wifi_disconnect()` aktiv abgebrochen,
eine BESTEHENDE Verbindung bleibt unangetastet), `einrichtung_wlan_aufraeumen()`
wirft ihn wieder an. Der Laufbetrieb-Neuscan (60s-Watchdog) pausiert mit.
Reproduziert und verifiziert auf Board 1 (Fantasie-SSID + geloeschtes NVS =
Eltern-Szenario): vor dem Oeffnen des Bildschirms lief die Reconnect-Schleife,
nach dem Antippen von "WLAN wechseln" kam "Verbindungsversuche pausiert" und die
Scans fanden sofort Netze (vorher: null).

**Zusaetzlich (zweite, unabhaengige Luecke):** Ohne explizite Laenderkennung
laeuft der WLAN-Treiber im "World Safe Mode" (Kennung "01") und scannt nur die
Kanaele 1-11 vollwertig - deutsche Router duerfen aber auch auf Kanal 12/13
senden und wuerden uebersehen. Deshalb in `netz_start()` jetzt
`esp_wifi_set_country_code("DE", true)` gesetzt (true = Kennung nach dem
Verbinden per 802.11d vom Router uebernehmen).

**Lehre:** Ein Feature, das nur in einer bestimmten Umgebungssituation gebraucht
wird (WLAN-Suche -> vor allem dann, wenn KEIN bekanntes Netz da ist), muss genau
in dieser Situation getestet werden - nicht nur in der Entwicklungsumgebung, wo
die Situation nie eintritt. Der Testaufbau dafuer war simpel (Fantasie-SSID in
secrets.h + NVS loeschen) und haette den Fehler vor dem Aufstellen gefunden.
Ausserdem: Wenn ein Code-Kommentar einen Fehlerfall bereits benennt ("Scan
schlaegt waehrend Verbindungsaufbau fehl"), lohnt die Frage, ob er nur ein
Symptom daempft oder die eigentliche Ursache unbehandelt laesst.

## 22. Status-Detail-Fenster: Textzeilen ueberlappten sich bei festen Y-Abstaenden

**Problem:** Im neuen Status-Detail-Fenster (Tipp auf die Status-Symbole, siehe FAHRPLAN
Nachtrag 16) ueberlappte die WLAN-Zeile die darunterliegende Uhrzeit-Zeile - "IP 192.168.188.45"
lag mitten in "Uhrzeit: synchronisiert".

**Ursache:** Bei diesem Projekt ist der kleinste UI-Font (`schrift_klein_28`, 28px, seniorengerecht
gross) deutlich breiter pro Zeichen als bei einer typischen Desktop-Schrift erwartet. Ein
Text wie "WLAN: Heimnetz (-70 dBm)" (25 Zeichen) passt bei 400px Zeilenbreite bereits NICHT
mehr in eine Zeile, sondern bricht auf zwei bis drei Zeilen um. Die erste Layout-Version hatte
feste Y-Abstaende (45px) geraten, die nur fuer eine einzeilige Anzeige gereicht haetten.

**Loesung:** `status_zeile_erzeugen()` liefert nach `lv_obj_update_layout(label)` die per
`lv_obj_get_height(label)` tatsaechlich ermittelte (umbruchabhaengige) Hoehe des soeben
erzeugten Labels zurueck; die naechste Zeile beginnt an dieser Position plus einem festen
Abstand. Damit ist es egal, ob eine Zeile 1, 2 oder 3 Zeilen braucht - es gibt nie eine
Ueberlappung. Per Screenshot verifiziert (erste Version ueberlappend, korrigierte Version
sauber gestapelt).

**Lehre:** Bei diesem Projekt NIE die Zeilenzahl/Hoehe eines mehrzeiligen Labels bei
`schrift_klein_28` (oder groesser) aus der Zeichenanzahl schaetzen und Positionen fest
verdrahten - stattdessen `lv_obj_update_layout()` + `lv_obj_get_height()` nutzen und
nachfolgende Elemente relativ dazu positionieren. Gilt fuer jedes neue UI-Element mit
variabler/unsicherer Textlaenge (Namen, IP-Adressen, Statusmeldungen), nicht nur fuer dieses
eine Fenster.

## 23. Label nach Umpositionierung unsichtbar: lv_obj_set_pos aendert NICHT das Style-Align

**Problem:** Beim Gross/Klein-Tausch der neuen Analoguhr (FAHRPLAN Nachtrag 17) sollte die
Digitaluhr klein an einen festen Punkt rechts wandern - stattdessen war sie nach dem Tausch
komplett unsichtbar. Ein erster Reparaturversuch (Position auf den sichtbaren Bereich
"klemmen") half nichts.

**Ursache:** Das Digitaluhr-Label war beim Aufbau mit `lv_obj_align(label, LV_ALIGN_TOP_MID,
0, 95)` positioniert worden. In LVGL 9 setzt `lv_obj_align()` ZWEI getrennte Dinge: das
Style-Align (hier TOP_MID) und die x/y-Offsets. Ein spaeteres `lv_obj_set_pos(label, x, y)`
aendert NUR die Offsets - das Align bleibt TOP_MID. Die vermeintlich "absoluten" Koordinaten
(z. B. x=655) wurden dadurch als Versatz von der oberen BildschirmMITTE interpretiert: linke
Labelkante bei 400+655, weit ausserhalb der 800px. Auch die Klemmung rechnete mit falschen
Annahmen und aenderte daran nichts.

**Loesung:** Vor dem absoluten Positionieren das Align explizit zuruecksetzen:
`lv_obj_set_align(label, LV_ALIGN_TOP_LEFT)` (der Default), erst danach `lv_obj_set_pos()`.
Der Rueckweg (gross) nutzt weiterhin `lv_obj_align()`, das Align und Offsets gemeinsam setzt.

**Lehre:** Sobald ein LVGL-Objekt einmal per `lv_obj_align()` mit etwas anderem als
TOP_LEFT ausgerichtet wurde, ist `lv_obj_set_pos()` fuer dieses Objekt KEINE absolute
Positionierung mehr. Bei jedem Objekt, das zwischen "ausgerichtet" (align) und "frei
positioniert" (set_pos) wechselt, muss der Wechsel das Align explizit mitfuehren. Typisches
Symptom: Objekt "verschwindet" nach einer Umpositionierung, obwohl die berechneten
Koordinaten auf dem Papier stimmen.

## 24. Absturz jede Nacht um Punkt 00:00 Uhr — Stack-Overflow der LVGL-Task beim Tageswechsel

**Problem:** Board 2 (bei den Eltern) hatte nachts wiederholt einen schwarzen Bildschirm;
die Absturz-Blackbox (#Diagnose-Feature, FAHRPLAN Nachtrag 18) meldete beim Neustart
"Programmabsturz", zuletzt aktiv "23:59 Uhr". Also ein Absturz taeglich um Mitternacht.

**Reproduktion (der halbe Weg zur Loesung):** Statt jede Nacht zu warten, wurde im
`sync_callback` (zeit.c) temporaer die Uhr direkt nach dem ersten NTP-Sync auf 23:59:55
gestellt (NACH dem Sync, sonst holt der naechste NTP-Poll sofort die echte Zeit zurueck - eine
ueber das Menue manuell gesetzte Zeit wurde live genau so ueberschrieben). Damit liess sich der
Absturz in ~2 Minuten pro Versuch zuverlaessig ausloesen und mit angehaengtem Monitor
(`esp_idf_monitor --no-reset`, damit die Blackbox-Meldung am Geraet stehen bleibt) der
komplette Backtrace + Coredump mitschneiden.

**Irrwege / Eingrenzung:** Die Absturzbilder wechselten von Lauf zu Lauf (`assert failed:
xTaskRemoveFromEventList pxUnblockedTCB`, `LoadProhibited`, `InstrFetchProhibited`, sogar im
GDMA-Interrupt-Handler) - typisch fuer eine Speicher-Korruption, bei der je nach Heap-Layout
ein anderer spaeterer Zugriff das Opfer wird, nicht die Ursache. Bisektion (Analoguhr-Zeiger-
Update auskommentiert) und ein Heap-Integritaets-Check bei jedem Sekunden-Tick zeigten: der
Heap ist bis 23:59:59 gesund und exakt beim 00:00:00-Tick korrupt. Backtrace an dem Tick immer
`uhr_tick -> tagesansicht_tag_aktualisieren -> button_terminfarbe_setzen ->
kalender_anzeige_eintraege_fuer_tag -> ics_parser`.

**Ursache (der rauchende Colt):** Eine Stack-High-Watermark-Messung am Ende von
`tagesansicht_tag_aktualisieren` lieferte direkt vor dem Crash **308 Byte frei** (normal
~5700). Klarer Stack-Overflow der LVGL-Task. Der Grund ist eine tiefe Aufrufkette mit mehreren
grossen lokalen Puffern gleichzeitig, die NUR beim Tageswechsel voll durchlaeuft:
`tagesansicht_tag_aktualisieren` faerbt alle 7 Wochentag-Buttons neu ein und ruft dafuer 7x
`button_terminfarbe_setzen`, das ueber `kalender_anzeige_eintraege_fuer_tag` jeweils einen
`ics_termin_t termine[32]`-Puffer (~3,8KB) anlegt - waehrend `uhr_tick` darueber schon seine
eigenen ~3,9KB Text-/Eintrags-Puffer haelt. Der 10K-Stack der LVGL-Task lief dabei bis auf 308
Byte leer und kippte in den benachbarten Heap. Weil `tagesansicht_tag_aktualisieren` an
normalen Ticks per Tages-Schluessel-Vergleich frueh zurueckkehrt und den teuren Pfad NUR bei
echtem Datumswechsel nimmt, trat der Absturz ausschliesslich um Punkt Mitternacht auf - und nie
beim normalen Boot-Test tagsueber. (Der Canary-Stack-Overflow-Schutz griff nicht sichtbar, weil
er nur beim Task-Switch prueft; hier crashte der wilde Heap-Zugriff schon innerhalb desselben
Ticks.)

**Loesung:** LVGL-Task-Stack in `anzeige.c` von 10240 auf 16384 Byte erhoeht (dieselbe
Fehlerklasse hatte den Stack schon einmal von 4K auf 10K getrieben, damals beim blossen
Antippen eines Buttons - derselbe Pfad, aber nur 1x statt 7x). Verifiziert per identischer
Test-Zeit-Reproduktion: der 00:00:00-Tageswechsel laeuft jetzt mit `tabl_geaendert=1` sauber
durch, Heap-OK, Stack-Reserve an der kritischen Stelle 5204 statt 308 Byte, kein Absturz ueber
mehrere Minuten Nachlauf.

**Lehre:** (1) Ein Absturz mit WECHSELNDEN Fehlerbildern (mal Assert, mal LoadProhibited, mal im
fremden Interrupt) ist fast immer Speicher-Korruption - nicht am Absturzort suchen, sondern die
Quelle per Heap-Check/Stack-Watermark einkreisen. (2) Ein Absturz zu einer festen Uhrzeit deutet
auf zeitgesteuerten Sonder-Code (hier Tageswechsel); den kann man per manuell gestellter Testzeit
in Minuten statt Stunden reproduzieren, wenn man NACH dem NTP-Sync setzt. (3) Bei ESP-IDF mit
Puffer-lastigen Callback-Ketten den Task-Stack GROSSZUEGIG bemessen und den tatsaechlichen
Verbrauch mit `uxTaskGetStackHighWaterMark` im Worst-Case-Pfad messen - der Canary allein warnt
nicht zuverlaessig, wenn die Korruption noch im selben Tick zuschlaegt.

---

## 25. OTA-Hintergrund-Task: PSRAM-Stack stuerzt beim Flash-Schreiben ab, interner SRAM reicht am Boot-Ende knapp nicht

**Problem (zwei getrennte Huerden beim Aufbau von `main/ota.c`):** Ein per `xTaskCreate()` am
Ende von `app_main()` gestarteter Hintergrund-Task fuer OTA-Updates scheiterte live mit
"OTA-Task konnte nicht gestartet werden" - `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`
direkt davor zeigte 13311 Byte frei, also augenscheinlich genug fuer die angeforderten 8192
Byte Stack.

**Erster (falscher) Loesungsversuch:** Stack per `xTaskCreateStatic` explizit aus dem PSRAM
alloziert (`heap_caps_malloc(...MALLOC_CAP_SPIRAM)`), analog zu den grossen Puffern in
`kalender_holen.c`/`screenshot_debug.c`. Task liess sich zwar erzeugen, stuerzte aber beim
ersten echten OTA-Versuch sofort ab: `assert failed:
esp_cache_freeze_caches_disable_interrupts esp_cache_utils.c:96
(s_task_stack_is_sane_when_cache_frozen())`. **Ursache:** Ein Task, der selbst Flash beschreibt
(hier: die neue Firmware in die OTA-Partition schreiben), braucht seinen Stack zwingend im
internen SRAM - waehrend des Flash-Schreibens wird der Cache kurz eingefroren, und PSRAM haengt
komplett am selben Cache/MMU-Mechanismus, ist in diesem Moment also fuer den Task selbst nicht
erreichbar. **Lehre:** Ein PSRAM-Stack ist NUR fuer Tasks geeignet, die garantiert nie Flash-
Operationen (NVS-Schreiben, `esp_https_ota_*`, `spi_flash_*`) direkt auf ihrem eigenen Stack
ausfuehren - im Zweifel interner SRAM, auch wenn PSRAM reichlich frei ist.

**Zweiter (richtiger) Loesungsversuch:** Stack zurueck auf normalen `xTaskCreate()` (interner
SRAM), aber die eigentliche Ursache des urspruenglichen Fehlschlags behoben: Der Aufruf direkt
am Ende von `app_main()` konkurriert dort mit dem eigenen, noch nicht freigegebenen 16-KB-Stack
des `main`-Tasks (`CONFIG_ESP_MAIN_TASK_STACK_SIZE`) sowie mit Fragmentierung durch die
WLAN-/TLS-Aktivitaet waehrend des Bootens - 13311 Byte gesamt frei heisst nicht, dass davon
8192 Byte in einem einzigen zusammenhaengenden Block liegen. **Loesung:** Start um 5s per
`esp_timer_create()`/`esp_timer_start_once()` verzoegert - sobald `app_main()` zurueckkehrt,
gibt der Idle-Task dessen Stack frei, und der Boot-Trubel hat sich gelegt. Seitdem erzeugt sich
der Task zuverlaessig.

**Lehre:** Bei ESP-IDF ist ein knapper interner SRAM am Ende eines WLAN-/TLS-lastigen
Boot-Vorgangs eher die Regel als die Ausnahme (siehe auch #20) - vor einer neuen, grossen
Task-Allokation lieber kurz auf einen Timer warten, statt den Speicher fest einzuplanen. Und:
PSRAM loest nicht jedes Speicherproblem - Tasks mit Flash-Zugriff brauchen ihren Stack zwingend
im internen SRAM, unabhaengig davon, wie viel PSRAM frei ist.

---

## 26. Sofortiger Absturz beim ersten Boot nach Erweiterung von ics_termin_t/kalender_tag_eintrag_t

**Problem:** Nach dem Hinzufuegen einer Beschreibung (`beschreibung[ICS_BESCHREIBUNG_MAX]`,
Ausbaustufe 2 des Erinnerungsfensters) zu `ics_termin_t` (ics_parser.h) und
`kalender_tag_eintrag_t` (kalender_anzeige.h) stuerzte das Geraet bei JEDEM Boot ab -
"Guru Meditation Error: Core 0 panic'ed (Unhandled debug exception)", exakt beim ersten
synchronen `uhr_tick()`-Aufruf in `app_main()`.

**Ursache:** `ics_termin_t` und `kalender_tag_eintrag_t` steckten schon vorher in mehreren
grossen Stack-Arrays, allen voran `ics_termin_t termine[32]` in `kalender_anzeige.c` (der schon
bei ~120 Byte/Eintrag mit ~3,8 KB der dominante Verursacher des Mitternachts-Stack-Overflows
aus FALLSTRICKE #24 war). Die neue 128-Byte-Beschreibung plus eine zweite `ics_zeit_t` fuer die
Endzeit (Punkt 4 derselben Ausbaustufe) liessen `ics_termin_t` von ~120 auf ~270 Byte anwachsen -
`termine[32]` damit von ~3,8 KB auf ~8,6 KB, mehr als doppelt so gross. Genau dieses Array liegt
in `kalender_anzeige_eintraege_fuer_tag()`, die `tagesansicht_tag_aktualisieren()` beim
Tageswechsel 7x verschachtelt aufruft (einmal pro Wochentag-Button) - derselbe Aufrufpfad, der
in FALLSTRICKE #24 schon einmal knapp wurde, diesmal aber durch groessere Eintraege statt durch
mehr Aufrufe gesprengt. Der erste `uhr_tick()`-Aufruf laeuft direkt im `main`-Task (16 KB Stack,
siehe FALLSTRICKE #25/#20), und genau dort passierte der allererste Tageswechsel-Durchlauf
jedes Boots.

**Loesung:** Beide `ics_termin_t termine[32]`-Arrays in `kalender_anzeige.c`
(`fuer_heute_neu_parsen()` und `kalender_anzeige_eintraege_fuer_tag()`) vom Stack in den PSRAM
verlagert (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` + `heap_caps_free()` vor jedem Return-Pfad) -
derselbe etablierte Griff wie bei den grossen Puffern in `kalender_holen.c`/`screenshot_debug.c`.
Zweimal in Folge sauber durchgebootet, kein Absturz mehr.

**Lehre:** Wird ein Struct groesser, das bereits in einem grossen Stack-Array steckt, IMMER
gegenpruefen, ob dieses Array (oder ein anderes desselben Structs) noch auf den Stack passt -
die Rechnung "Anzahl Eintraege × Struct-Groesse" macht das in Sekunden, und diese Codebasis hat
schon zweimal (#24, #26) genau diese Art von Stack-Overflow erlebt. Ein Feld "nur mal eben"
anzuhaengen ist in Structs, die in `[N]`-Arrays auf dem Stack liegen, nie kostenlos.

## 27. Fenster-Layout: unsichtbares Innenpolster des Panels frass alle eingeplanten Abstaende

**Problem:** Im "Heute"-Fenster schnitt die letzte Tabletten-Zeile in die OK-/Abbrechen-Buttons
hinein, obwohl zwischen Liste und Buttons rechnerisch ein Abstand eingeplant war. Schlimmer:
die scrollbare Liste liess sich gar nicht scrollen, obwohl sichtbar mehr Inhalt vorhanden war
als hineinpasste.

**Ursache:** Die Geometrie war durchgehend aus `FENSTER_BREITE`/der gesetzten Fensterhoehe
gerechnet. Ein per `lv_obj_create()` erzeugtes Panel behaelt aber (ohne
`lv_obj_remove_style_all()`) das Standard-Innenpolster des Themes - hier rund 20px je Seite,
plus 2px Rahmen. Die tatsaechliche INHALTSflaeche ist damit rund 44px niedriger und 44px
schmaler als das Panel. Der Listenbereich bekam per `lv_obj_set_size(..., HOEHE)` zwar seine
volle Wunschhoehe, sass aber in einem entsprechend kleineren Inhaltsbereich und ragte unten in
den Button-Bereich hinein. Weil LVGL die Liste zugleich fuer "passt genau" hielt, gab es aus
seiner Sicht nichts zu scrollen - die Scrollleiste blieb aus (`LV_SCROLLBAR_MODE_AUTO`).
Dasselbe Polster verschob auch die rechte Kante, weshalb eine aus `FENSTER_BREITE` gerechnete
Breite nie mit `lv_obj_align(..., LV_ALIGN_TOP_RIGHT, ...)`-positionierten Kindern zusammenpasste
(verwandt mit #23: gerechnete Koordinaten vs. Align-Mechanik).

**Loesung:** Nicht mehr gegen unsichtbare Werte rechnen, sondern sie zur Laufzeit messen bzw.
umgehen:
- Fensterhoehe aus der GEWUENSCHTEN Inhaltshoehe ableiten:
  `rahmen_polster = lv_obj_get_height(panel) - lv_obj_get_content_height(panel)` nach einem
  `lv_obj_update_layout()`, dann `lv_obj_set_height(panel, wunsch + rahmen_polster)`.
- Breiten relativ statt absolut: `lv_obj_set_size(inhalt, lv_pct(100), ...)` statt
  `FENSTER_BREITE - x`.
- Reservierte Streifen (hier fuer die Scrollleiste) als `pad_right` des Containers umsetzen -
  dann enden `LV_ALIGN_TOP_RIGHT`-Kinder automatisch davor, ohne Zusatzrechnung.
- Zeilen im Scrollbereich bei y=0 beginnen lassen und die sichtbare Hoehe als exaktes
  Vielfaches der Zeilenhoehe waehlen, sonst endet die letzte Zeile angeschnitten.

**Lehre:** Sobald in einem LVGL-Container mit gesetzten Pixelkoordinaten gearbeitet wird, ist
die Panel-Groesse NICHT die nutzbare Flaeche. Entweder `lv_obj_remove_style_all()` (dann ist
Polster = 0 und die Rechnung stimmt), oder konsequent `lv_pct`/`pad`/`align` verwenden, oder
das Polster einmal messen und einrechnen. Ein "es fehlen ein paar Pixel"-Symptom hat in dieser
Codebasis fast immer diese Ursache.

## 28. Abendliches Abdunkeln kam "ueberraschend": Wachzeit sah Beruehrungen in Fenstern nicht

**Problem:** Peter meldete, dass die Anzeige abends/nachts scheinbar zufaellig in den
abgedunkelten Modus wechselte, obwohl sie erst 30 Sekunden nach der letzten Beruehrung
abdunkeln sollte.

**Ursache:** Die Wachzeit wurde von einem `LV_EVENT_PRESSED`-Callback auf dem HAUPTBILDSCHIRM
verlaengert (`s_wach_bis_us`). Beruehrungen innerhalb der Fenster - Checkboxen, OK/Abbrechen,
Scrollen - werden dort von den Kind-Objekten verarbeitet und erreichen den Bildschirm nie. Die
30s liefen also waehrend der gesamten Bedienung ungebremst ab. Verdeckt wurde das durch den
Sonderfall "solange ein Fenster offen ist, gilt Tag-Modus": Erst beim SCHLIESSEN des Fensters
fiel dieser Schutz weg, und die laengst abgelaufene Wachzeit schlug schlagartig durch. Wie
ueberraschend das wirkte, hing davon ab, wie lange man VOR dem Oeffnen nichts angefasst hatte -
daher der zufaellige Eindruck.

**Loesung:** Eigenen Callback und `s_wach_bis_us` ersatzlos entfernt, stattdessen LVGLs eigene
Inaktivitaets-Uhr abgefragt: `lv_display_get_inactive_time(NULL) < BERUEHRUNG_WACHZEIT_MS`.
Die zaehlt jede Eingabe auf Ebene des Eingabegeraets, unabhaengig davon, welches Objekt sie
entgegennimmt.

**Lehre:** "Letzte Benutzeraktivitaet" ueber Event-Callbacks einzelner Objekte zu erfassen ist
strukturell lueckenhaft - jedes neu hinzugefuegte Bedienelement muesste daran denken. Wenn das
Framework eine globale Inaktivitaetszeit anbietet, ist sie der richtige Anker. Und: ein
Sonderfall, der einen Fehler nur VERDECKT ("Fenster offen => immer hell"), macht ihn beim
Wegfallen dieses Sonderfalls umso verwirrender.

## 29. sdkconfig.defaults wirkt NICHT auf eine bereits vorhandene sdkconfig - Rollback-Schutz war monatelang aus

**Problem:** `sdkconfig.defaults` schrieb seit dem OTA-Umbau
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` vor, mit dem Kommentar "fuer ein Geraet, das weit
entfernt bei den Eltern steht, die wichtigste einzelne Absicherung des ganzen OTA-Vorhabens".
In der tatsaechlich wirksamen `sdkconfig` stand aber `# CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is
not set`. Die Absicherung war damit in JEDER lokal gebauten Firmware inaktiv - auch in der, die
frisch auf das Geraet der Eltern geflasht wurde.

**Ursache:** `sdkconfig.defaults` ist eine Vorlage fuer die ERSTERZEUGUNG von `sdkconfig`. Ist
`sdkconfig` bereits vorhanden (bei diesem Projekt der Normalfall, die Datei ist gitignored und
lebt lokal weiter), gewinnen deren Werte - neu hinzugefuegte Zeilen in `sdkconfig.defaults`
werden schlicht ignoriert, ohne Warnung. Die Zeile war also von Anfang an wirkungslos.

**Wichtige Differenzierung:** Der GitHub-Actions-Build ist NICHT betroffen. Er checkt frisch aus,
hat also keine `sdkconfig` (gitignored) und erzeugt sie aus den Defaults - Release-Binaries hatten
den Rollback-Schutz immer. Falsch waren ausschliesslich die lokal gebauten und per USB geflashten
Stande. Genau deshalb faellt so etwas nicht auf: die "offizielle" Firmware ist korrekt, nur die
Entwickler-Builds weichen ab.

**Loesung:** `sdkconfig` geloescht und per `idf.py reconfigure` neu erzeugen lassen. Vorher gegen
die alte Datei diffen - hier war die Rollback-Zeile die EINZIGE Abweichung, ein Neuerzeugen also
gefahrlos. Danach beide Boards neu flashen.

**Lehre:** Nach jeder Aenderung an `sdkconfig.defaults` pruefen, ob der Wert auch in `sdkconfig`
angekommen ist - eine Zeile dort ist keine Garantie, sondern nur ein Wunsch. Ein Einzeiler
genuegt: jede `CONFIG_`-Zeile der Defaults gegen `sdkconfig` gegenpruefen und Abweichungen
melden. Besonders heikel bei Sicherheitsnetzen, die man erst im Ernstfall vermisst - der
Rollback-Schutz waere genau dann aufgefallen, wenn ein fehlerhaftes Update das Geraet weit
entfernt lahmgelegt haette.

## 30. Endlosschleife in app_main() legte den OTA-Task lahm - das Update-Symbol erschien nie

**Problem:** Nach dem Flashen erschien weder das Update-Symbol auf dem Hauptbildschirm noch die
Versions-Auswahlliste im Einstellungen-Menue, obwohl kurz zuvor noch
`ota: Neue Version verfuegbar: v0.9.0` im Log gestanden hatte.

**Ursache:** Im Log stand bei JEDEM Boot `W ota: OTA-Task konnte nicht gestartet werden - Updates
bleiben bis zum naechsten Neustart aus`. Es gab also gar keine Pruefung. Ausgeloest hatte das die
unmittelbar vorhergehende Aenderung: um das Einstellungen-Menue nach dem Booten erreichbar zu
machen, blieb `app_main()` als Endlosschleife am Leben. Damit gibt der Idle-Task dessen 16 KB
internen SRAM (`CONFIG_ESP_MAIN_TASK_STACK_SIZE`) nie frei - genau das, worauf der 5 s spaeter
startende OTA-Task fuer seine 8 KB angewiesen ist (siehe #25). Der Zusammenhang war in ota.c
sogar auskommentiert und wurde trotzdem uebersehen.

**Loesung:** `app_main()` kehrt wieder zurueck; der Menue-Ablauf laeuft in einem eigenen Task
(siehe #31).

**Lehre:** Interner SRAM ist die knappe Ressource dieses Projekts (#20, #25, #26, #29). **Jede
Aenderung, die einen Task dauerhaft am Leben haelt, kostet dessen kompletten Stack aus genau
diesem Topf.** Vor so einer Aenderung pruefen, wer sonst noch internen SRAM braucht - und wann.
Und: Ein Fehler, der bei JEDEM Boot identisch auftritt, steht meist woertlich im Log. Erst lesen,
dann Hypothesen bilden.

## 31. Task-Stack aus dem Heap ist eine Lotterie - statisch im .bss ist die Loesung

**Problem:** Das Einstellungen-Menue liess sich per Tipp nicht mehr oeffnen. Der Callback feuerte
sauber (im Log belegt), aber `xTaskCreate` scheiterte - erst mit 12 KB Wunsch (groesster freier
interner Block: 8704 Byte), nach dem eingebauten Rueckfall auch mit 8 KB (5632 Byte).

**Ursache:** Der Task wurde auf Zuruf erzeugt, also ausgerechnet im laufenden Betrieb, wenn der
interne SRAM am staerksten zerstueckelt ist. Frei waren insgesamt ~17 KB, der groesste
zusammenhaengende Block aber nur ein Drittel davon.

**Loesung:** Stack statisch als `StackType_t`-Array im `.bss` plus `xTaskCreateStatic`. Der Platz
steht beim Binden fest, kann nicht fragmentieren, das Erzeugen kann nicht fehlschlagen. Weil er
ohnehin dauerhaft belegt ist, lebt der Task auch dauerhaft und wartet blockierend per
`ulTaskNotifyTake` - das erspart zugleich den heiklen Fall, dass ein zweiter Tipp den Task neu
anlegt, waehrend der alte noch abgeraeumt wird.

**Gegenfinanziert durch Messen statt Schaetzen:** Der Kalender-Task hatte 16 KB, dimensioniert
BEVOR sein grosses `ics_termin_t`-Array in den PSRAM wanderte (#26). Gemessen waren davon 10060
Byte nie angefasst, der echte Bedarf lag bei gut 6,3 KB - gekuerzt auf 10 KB, nach dem Umbau mit
4076 Byte Reserve bestaetigt.

**Messmethode (ohne zusaetzliche Konfiguration):**

```c
TaskHandle_t t = xTaskGetHandle("kalender");
if (t)
    ESP_LOGI(TAG, "ungenutzte Reserve: %u Byte",
             (unsigned)(uxTaskGetStackHighWaterMark(t) * sizeof(StackType_t)));
```

Braucht **kein** `CONFIG_FREERTOS_USE_TRACE_FACILITY`.

**Wichtige Einschraenkung:** Der Wert sagt nur etwas aus, wenn der Task seine Arbeit auch getan
hat. Der `httpd`-Task meldete ~6,9 KB ungenutzt - allerdings nur, weil in dem Durchlauf niemand
die Weboberflaeche aufgerufen hatte. Genau dort gab es frueher einen Stack-Overflow (#18). Eine
Messung an einem untaetigen Task misst nichts.

## 32. Nach dem ersten OTA-Update war kein WLAN mehr moeglich - Release-Firmware enthaelt nur Platzhalter

**Problem:** Das erste echte OTA-Update lief technisch einwandfrei durch (Download, Einspielen,
Neustart) - danach hatte das Geraet keine Internetverbindung mehr. Der Weg zurueck gelang nur
von Hand ueber die Versions-Auswahlliste im Menue.

**Ursache:** Der Release-Workflow ersetzt `main/secrets.h` bewusst durch `secrets.example.h`,
damit kein echtes WLAN-Passwort in einer oeffentlichen Binary landet. In der Release-Firmware
steht deshalb `WLAN_SSID "Netzwerkname eintragen"`. Ein Geraet, das kein gespeichertes Profil im
NVS hat und ueber `secrets.h` verbindet (im Log: `Zugangsdaten: secrets.h (im Scan gefunden)`),
verliert damit **mit jedem Update** seinen Netzzugang.

**Der gefaehrliche Teil:** Ohne Netz kommt nie wieder ein Update an. Das Geraet kann sich aus
diesem Zustand nicht selbst befreien - es braucht ein USB-Kabel oder manuelle WLAN-Eingabe am
Touchscreen. Bei einem weit entfernten Geraet bedeutet das eine Fahrt.

**Zweite Ursache, gleich daneben:** Die Rollback-Bestaetigung prueft zwar korrekt auf WLAN UND
Kalender, wartete darauf aber **ohne Zeitgrenze**. Eine Firmware, die zwar startet, aber kein
Netz bekommt, wurde damit weder bestaetigt noch zurueckgenommen - das Geraet lief einfach
dauerhaft offline weiter. Der Sicherheitsgurt war angelegt, aber nicht eingerastet.

**Loesung:** (1) Sobald eine Verbindung ueber `secrets.h` nachweislich steht (erst bei
`IP_EVENT_STA_GOT_IP`, also nur mit funktionierenden Daten - Platzhalter koennen sich so nie
einnisten), wandern die Zugangsdaten einmalig in den NVS. Die Partition wird von OTA nicht
angefasst, das ueberlebt jedes Update. (2) Bewaehrungsfrist von 10 Minuten; laeuft sie ab, ruft
das Geraet `esp_ota_mark_app_invalid_rollback_and_reboot()` und der Bootloader holt die
vorherige Version zurueck.

**Lehre:** Wenn ein Build-Schritt bewusst etwas aus der Firmware entfernt (hier: Zugangsdaten),
dann durchdenken, worauf das laufende Geraet sonst noch angewiesen ist. Und: Ein Rollback-Netz,
das auf eine Bedingung wartet, braucht immer eine Zeitgrenze - sonst wartet es im Ernstfall ewig.

## 33. "stack overflow in task sys_evt" - im Ereignis-Handler ist fast kein Stack

**Problem:** Direkt nach dem Fix aus #32 stuerzte das Geraet ab. Die Absturz-Blackbox zeigte beim
naechsten Boot "Programmabsturz", der serielle Log den Grund:
`***ERROR*** A stack overflow in task sys_evt has been detected.`

**Ursache:** Der neue NVS-Schreibweg haengt am WLAN-Ereignis-Handler, und der laeuft im Task
`sys_evt` mit **2304 Byte** Stack (`CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE`). Die Aufrufkette
legt drei `wlan_profil_t[5]`-Arrays uebereinander an - `wlan_profil_t` ist 98 Byte, macht
490 Byte je Array, zusammen ~1470 Byte, zuzueglich allem was NVS selbst braucht. Der Ueberlauf
war rechnerisch unvermeidlich.

**Loesung:** Der Handler setzt nur noch einen Merker (`netz_wartung_faellig()`), ausgefuehrt wird
die Arbeit in einem Task mit ordentlichem Stack - hier der ohnehin wartende Einstellungen-Task
mit 8 KB (siehe #31), der dafuer mit Zeitgrenze statt unbegrenzt blockiert.

**Lehre:** Ereignis-Handler (WLAN, IP, esp_timer) laufen auf fremden, knapp bemessenen Stacks -
dort gehoert nur Zustand gesetzt, keine Arbeit erledigt. Das ist zugleich die vierte Wiederholung
desselben Musters in diesem Projekt (#8, #24, #26): **bei jedem Stack-Array `[N]` die Rechnung
Anzahl x Groesse gegenpruefen, und dabei die ganze Aufrufkette betrachten, nicht die einzelne
Funktion.**

## 34. Fenster auf dem falschen Screen erzeugt - der Update-Knopf wirkte tot

**Problem:** Der Update-Knopf im Einstellungen-Menue schien wirkungslos. Im Log loeste er sauber
aus (`Update im Einstellungen-Menue angestossen`), auf dem Bildschirm passierte nichts.

**Zwei unabhaengige Ursachen:**
1. Das Fortschrittsfenster wurde per `lv_obj_create(s_scr)` auf dem **Uhren-Bildschirm** erzeugt.
   Angestossen wird das Update aber aus dem Einstellungen-Menue, einem eigenen Screen - das
   Fenster war dort selbst bei laufendem Download unsichtbar.
2. Das Fenster erschien ueberhaupt erst, wenn der Download bereits lief (`s_laeuft` wurde erst
   nach erfolgreichem Verbindungsaufbau gesetzt). Scheiterte schon die Verbindung, gab es nie
   irgendeine Rueckmeldung.

**Loesung:** Fenster per `lv_obj_set_parent(..., lv_layer_top())` ueber alle Screens legen
(dasselbe Muster wie beim Screenshot-Button). `s_laeuft` steht ab dem Tastendruck, mit
Klartext-Meldung statt Prozentbalken, solange es noch keinen Fortschritt gibt.

**Lehre:** Bei Overlays immer fragen, auf WELCHEM Screen sie liegen und welcher gerade geladen
ist. Und: **Ein Knopf, der schweigend scheitert, ist schlimmer als einer, der eine Fehlermeldung
zeigt** - erst recht bei Nutzern, die nicht ins Log sehen koennen. Jede Aktion braucht sichtbare
Rueckmeldung ab dem ersten Moment, nicht erst wenn sie gelingt.

## 35. Serieller Mitschnitt: ohne `python -u` bleibt die Log-Datei minutenlang leer

**Problem:** Ein im Hintergrund laufender Mitschnitt
(`python -m esp_idf_monitor ... | Out-File log.txt`) schrieb ueber eine Stunde lang **0 Byte**.
Screenshots und Log-Zeilen schienen nicht anzukommen; erst beim Beenden des Prozesses fielen auf
einen Schlag 42 KB heraus.

**Ursache:** Python puffert seine Ausgabe blockweise, sobald sie nicht auf ein Terminal geht.

**Loesung:** `python -u -m esp_idf_monitor --no-reset --port COM3 build/seniorenuhr.elf`.

**Nebenbei zwei Werkzeug-Fallen:** `tools/screenshot_dekodieren.py` nimmt bei mehreren
Screenshots im Log den ERSTEN - fuer den neuesten den Abschnitt ab der letzten
`-----BEGIN SCREENSHOT`-Zeile in eine eigene Datei schneiden. Und beim Durchsuchen der Logs mit
`grep` das Flag `-a` verwenden: die Dateien enthalten NUL-Bytes, sonst meldet grep nur
"Binary file matches".

## 36. LVGL-Dropdown zeigte ein leeres Kaestchen statt eines Pfeils

**Problem:** Im Versions-Auswahlfeld des Einstellungen-Menues stand rechts ein leeres Rechteck.

**Ursache:** LVGL zeichnet dort standardmaessig `LV_SYMBOL_DOWN`. Die Schriften dieses Projekts
sind Montserrat-Ableitungen **ohne Symbolglyphen** - die Glyphe fehlt und wird als "Tofu"-Box
dargestellt. Genau dieselbe Ursache, aus der die abgehakten Tabletten mit `"[x] "` statt einem
Haken angezeigt werden.

**Loesung:** `lv_dropdown_set_symbol(dd, NULL)`.

**Lehre:** Jedes LVGL-Widget, das von sich aus ein Symbol zeichnet, ist in diesem Projekt
verdaechtig. Bei neuen Widgets gezielt darauf achten und das Symbol abschalten oder durch Text
ersetzen.

## 37. Schnelle Abfrage hinter langsamer Abfrage in der Schlange - die Auswahlliste kam immer zu spaet

**Symptom:** Im Einstellungen-Menue fehlte die Versionsliste hartnaeckig. Beim Start ueber das
Zahnrad blieb sie ausnahmslos leer; erst wer das Menue Minuten spaeter noch einmal oeffnete, sah
sie. Nichts deutete auf einen Fehler hin - der Bereich meldete brav "Suche nach Updates...".

**Ursache:** Der OTA-Task erledigte beides nacheinander in EINEM Zug, aber in der falschen
Reihenfolge: erst `pruefung_mit_wiederholung()`, dann `ota_versionen_abfragen()`. Die Pruefung
braucht im schlechtesten Fall drei Anlaeufe mit je 20 s Pause - die Liste stand also hinter der
langsamsten Stelle des ganzen Moduls in der Schlange.

Im Log (zwei aufeinanderfolgende Startvorgaenge, identische Firmware) war das exakt
reproduzierbar:

```
W (120658) ota: Update-Pruefung nach 3 Versuchen aufgegeben
I (123310) ota: 2 Version(en) im Download-Repo gefunden     <- erst hier
...
W (113979) ota: Update-Pruefung nach 3 Versuchen aufgegeben
I (123768) ota: 2 Version(en) im Download-Repo gefunden     <- und hier
```

Beide Male 123 Sekunden. Die Boot-Phasen laufen aber nach 60 s in ihren Timeout - das Menue
konnte die Liste dort gar nicht sehen. Bitter dabei: die Listen-Abfrage selbst ist EIN einzelner
API-Aufruf und ging jedes Mal im ERSTEN Versuch durch. Nur die Prueferei davor scheiterte.

**Loesung:** Reihenfolge umgedreht - Liste zuerst, Pruefung danach (an allen drei Stellen im
Task: erster Lauf, Anstoss aus dem Menue, 30-Minuten-Intervall). Danach gemessen: Liste bei 65 s
statt 123 s, und bei einem Anstoss aus dem Menue binnen Sekunden.

**Lehre:** Wenn eine schnelle und eine langsame Netzabfrage im selben Task nacheinander laufen
und die Oberflaeche auf die SCHNELLE wartet, entscheidet allein die Reihenfolge darueber, ob das
Ergebnis rechtzeitig ankommt. Vor dem Bauen fragen: worauf wartet der Benutzer wirklich? Das
gehoert nach vorn. Ein Nebeneffekt derselben Bauart: `ota_pruefung_laeuft()` deckt beide Schritte
ab, die Oberflaeche meldete also die ganzen zwei Minuten lang wahrheitsgemaess "Suche laeuft" -
eine korrekte Meldung kann einen Konstruktionsfehler perfekt tarnen.

## 38. Erster Netzzugriff eines frisch gestarteten Tasks scheitert regelmaessig - Wiederanlauf zu spaet

**Symptom:** Das Kalender-Symbol auf dem Hauptbildschirm blieb nach dem Start eine gute halbe
Minute durchgestrichen ("keine Sync"), obwohl WLAN und Uhrzeit laengst standen.

**Ursache:** Der Kalender-Task greift 23 ms (!) nach seinem eigenen Start zum Netz - also
mitten in den Boot-Phasen, waehrend der Startbildschirm den internen SRAM noch belegt. Dieser
allererste Versuch scheitert verlaesslich mit `ESP_ERR_HTTP_CONNECT`; der naechste Versuch, nach
dem Bildschirmwechsel, gelingt jedes Mal auf Anhieb. Nur wartete der Task dazwischen stur
`ABRUF_RETRY_US` = 30 Sekunden.

`kalender_anzeige_frisch()` verlangt bewusst einen echten Download in DIESER Sitzung (ein
gecachter Kalender zaehlt nicht) - also blieb das Symbol genau diese 30 s durchgestrichen. Die
Boot-Phase meldete unterdessen "Schritt Kalender fertig (version=1)", weil ihr der Cache
genuegt; der Fehlschlag fiel dort nicht auf.

**Loesung:** Gestaffelte Pause statt fester 30 s - die ersten drei Fehlschlaege warten nur 5
Sekunden (`ABRUF_KURZ_RETRY_US`), danach greift wieder die volle Pause. Gemessen: Fehlschlag bei
15674 ms, Erfolg bei 21971 ms - 6 statt 30 Sekunden.

**Lehre:** Der erste Netzzugriff nach dem Boot ist ein Sonderfall, kein Normalbetrieb. Eine
Wiederholungspause, die fuer den Dauerbetrieb sinnvoll ist (Netz schonen), ist beim Anlauf viel
zu lang. Staffeln: kurz beginnen, dann verlaengern.

## 39. Dauerhaft laufender Webserver + mDNS frassen den internen SRAM - Ursache der monatelangen GitHub-Verbindungsabbrueche

**Symptom:** Zwei scheinbar getrennte Probleme, dieselbe Ursache. Erstens die seit Wochen
gesuchte Hauptbaustelle: OTA-Verbindungen zu github.com scheiterten reproduzierbar, obwohl
`esp-x509-crt-bundle: Certificate validated` jedes Mal erschien - der Handshake kam durch, aber
danach brach es ab. Zweitens (an einem Screenshot entdeckt): eine Auswahlliste fehlte weiterhin,
obwohl die Reihenfolge-Korrektur aus #37 bereits eingespielt war - auf einem anderen Board als
erwartet getestet (zwei Boards haengen am selben PC, COM3 UND COM5 - Verwechslungsgefahr).

**Ursache:** Direkt an der Quelle gemessen (`webkonfig_start()`, vor/nach `mdns_init()`/
`httpd_start()`):

```
vor  mdns_init:    frei intern 19995, groesster Block 14336
nach mdns_init:    frei intern 15087, groesster Block  9728    ->  -4,9 KB
nach httpd_start:  frei intern  2959, groesster Block  1920    -> -12,1 KB
```

Der lokale Webserver fuer die Kalender-URL-Konfiguration (`webkonfig.c`) lief seit seiner
Einfuehrung DAUERHAFT, gestartet bei jeder WLAN-Verbindung. Sein Task-Stack (8192 Byte, wegen
eines frueheren Stack-Overflows im Formular-Handler verdoppelt) plus mDNS zusammen fressen ca.
17 KB - und Task-Stacks liegen zwingend im internen RAM, nicht im PSRAM. Uebrig blieben 2959
Byte. Kurz danach: `lwip_arch: thread_sem_init: out of memory`, und **schon die
Namensaufloesung** scheiterte fuer github.com, api.github.com UND calendar.google.com
(`getaddrinfo() returns 202`). Erklaert in einem Rutsch: monatelange GitHub-Verbindungsabbrueche
trotz validiertem Zertifikat, gelegentliche Kalender-Download-Fehlschlaege, fehlende
Versionsliste.

**Zwei Irrwege beim Eingrenzen, fuers naechste Mal:**
1. Erst wurde das Einstellungen-Menue selbst verdaechtigt. Falsch: sein Aufbau kostet NULL Heap
   (19999 Byte davor wie danach) - er kommt komplett aus LVGLs GETRENNTEM 64-KB-Pool.
   **Heap und LVGL-Pool immer getrennt ausweisen** (`lv_mem_monitor()` vs. `heap_caps_get_free_size`),
   sonst sucht man an der falschen Stelle.
2. Eine Logdatei enthielt zwei Startvorgaenge; ohne auf die (nicht monoton laufenden)
   Zeitstempel zu achten, sah das wie ein doppelt gefeuerter Ereignis-Handler aus. **Bei
   Zeitstempeln immer zuerst die Boot-Grenzen suchen** (`grep "App version"`), dann interpretieren.

**Loesung:** Der Webserver startet jetzt NUR AUF ZURUF aus dem Einstellungen-Menue heraus (neuer
Knopf "Weboberflaeche einschalten"/"ausschalten" in `einrichtung.c`), nicht mehr automatisch bei
jeder WLAN-Verbindung. Der automatische Aufruf in `netz.c`s Ereignis-Handler ist entfernt. Beim
Verlassen des Menues (`einrichtung_einstellungen_aufraeumen()`) wird er sicherheitshalber wieder
ausgeschaltet - er soll den Menue-Bildschirm nie unbemerkt ueberleben. Die Eltern bekommen den
Knopf nie zu Gesicht (das Menue ist ohnehin nur per 5-Sekunden-Halten + Bestaetigungsdialog
erreichbar, siehe app_main.c). Gemessen nach dem Umbau: **35467 Byte frei intern** im
Normalbetrieb statt der bisherigen ~19000 - fast das Doppelte.

**Lehre:** Ein Feature, das "praktisch nichts kostet, wenn es niemand benutzt" (hier: ein
Webserver, der die meiste Zeit einfach nur lauscht), kann trotzdem staendig Ressourcen BINDEN,
die andere Teile des Systems dringend brauchen. Auf einem Geraet mit knappem internem SRAM
gehoert jede Wartungsfunktion, die nicht zum Kernbetrieb zaehlt, standardmaessig AUS und wird nur
auf Zuruf eingeschaltet - dieselbe Grundhaltung, die vorher schon beim Einstellungen-Menue selbst
galt (5-Sekunden-Halten statt Dauerzugriff).

**Zweite Lehre (Board-Verwechslung):** Zwei baugleiche Boards am selben PC (COM3 = Dev, COM5 =
Eltern-Testgeraet) sind per Log ab sofort an der WLAN-Station-MAC unterscheidbar - sie steht seit
diesem Fix in der allerersten Boot-Zeile (`app_main.c`). Vorher stand sie erst deutlich spaeter
in `wifi:mode : sta (...)`, und ein Log konnte leicht dem falschen Board zugeschrieben werden.
**Vor jedem Flash klaeren, an welchem Port der Nutzer gerade sitzt** - sonst testet er
versehentlich eine Firmware, die die untersuchte Korrektur gar nicht enthaelt.

## 40. Core-Dump nur ueber UART: der Absturz, der am wichtigsten gewesen waere, war nicht analysierbar

**Symptom:** Ein echter Absturz auf dem Eltern-Board (Blackbox-Anzeige: "Programmabsturz,
Absturz Nr. 2") liess sich nicht untersuchen - kein Backtrace, keine Task-Zustaende, gar nichts.

**Ursache:** `CONFIG_ESP_COREDUMP_ENABLE_TO_UART=y` schreibt den Dump auf die serielle Leitung.
Das setzt ein angestecktes Kabel UND einen mitlaufenden Monitor voraus. Genau das ist beim
Testen am Router aber unmoeglich: Peters Arbeitsplatz hat dort schlechten Empfang, das Board
muss 5 m weiter getragen und an einer Powerbank betrieben werden. Der Dump ging also in dem
Moment verloren, in dem er am meisten wert gewesen waere - beim einzigen Absturz, der sich nur
unter genau diesen Bedingungen zeigt.

**Loesung:** `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y` plus eine eigene `coredump`-Partition
(`partitions.csv`). Der Dump wird im Panic-Handler in den Flash geschrieben, uebersteht Neustart
und Stromausfall und wird beim naechsten Einstecken ausgelesen:

```powershell
idf.py -p COM3 coredump-info     # Backtrace + alle Task-Stacks im Klartext
idf.py -p COM3 coredump-debug    # dasselbe interaktiv in gdb
```

Drei Punkte, die dabei wichtig waren:

1. **Partition ans ENDE haengen.** Alle bestehenden behalten so ihre Adressen - NVS
   (WLAN-Profile) und `speicher` (Kalender-Cache) ueberstehen die Umstellung unveraendert.
   Hinter `speicher` lagen 1728 KB Flash ungenutzt brach; eine SD-Karte (die urspruengliche
   Ueberlegung) ist dafuer nicht noetig.
2. **256 KB statt der ueblichen 64 KB.** Der Dump sichert die Stacks ALLER Tasks, und dieses
   Projekt hat davon reichlich grosse (LVGL 16K, main 16K, kalender 10K, ...). Ein zu kleiner
   Puffer schneidet den Dump ab - genau dann, wenn man ihn braucht.
3. **`CONFIG_ESP_COREDUMP_STACK_SIZE` setzen** (hier 1792, das erlaubte Minimum). Ohne eigenen
   Stack laeuft der Dump-Vorgang auf dem Stack der abgestuerzten Task. Die haeufigste
   Absturzursache in diesem Projekt war aber ein Stack-Overflow (#10/#18/#24/#33) - auf genau
   diesem beschaedigten Stack waere der Dump unbrauchbar. ESP-IDF warnt beim Konfigurieren,
   wenn der Wert (Default 0) ausserhalb des gueltigen Bereichs liegt; diese Warnung ernst nehmen.

**Live verifiziert statt gehofft:** Ein absichtlicher `abort()` in `app_main()` wurde
eingebaut, geflasht, der Dump danach aus dem Flash ausgelesen (korrekte Quellzeile,
Absturzgrund, Register, Stacks aller Tasks) und der Testcode wieder entfernt. **Eine
Diagnose-Einrichtung, auf die man sich im Ernstfall verlassen will, muss einmal absichtlich
ausgeloest worden sein** - sonst stellt sich erst beim echten Absturz heraus, dass sie nicht
greift. Dabei fiel auch auf, dass ESP-IDF 5.5 kein `coredump-erase` kennt (Loeschen von Hand:
`python -m esptool -p COMx erase_region 0x650000 0x40000`).

## 41. Bewaehrungsprobe blockierte den ganzen OTA-Task - und haette gesunde Updates zurueckgerollt

**Symptom (Peters Meldung, zweimal):** Direkt nach einem Update zeigte das Einstellungen-Menue
dauerhaft "Kein Update verfuegbar / Keine vorherige Version / (keine)" - obwohl auf GitHub ein
Release lag und im anderen OTA-Slot sehr wohl eine vorherige Version stand. Peter markierte das
beim ersten Mal ausdruecklich mit "!!!". Meine erste Erklaerung ("die Pruefung braucht ~60 s,
du warst zu schnell im Menue") war **falsch** - ein spaeter aus dem Flash abgeholter Screenshot
trug den Zeitstempel "Boot-Zeit 153399 ms", also weit jenseits der 60 s.

**Ursachenkette** (im Log belegt, danach im Code verifiziert):

1. `kalender_task_starten()` steht in `app_main.c` ERST NACH den Boot-Phasen WLAN und Uhr.
2. Wer ueber das Zahnrad des Startbildschirms ins Einstellungen-Menue geht, haelt den Ablauf
   genau davor an - der Kalender-Task wird nie erzeugt, `kalender_anzeige_version()` bleibt 0.
3. `rollback_bestaetigen_falls_noetig()` lief als ERSTES im OTA-Task und blockierte dort in
   `while (!(netz_ist_verbunden() && kalender_anzeige_version() != 0))`.
4. Nach jedem Update UND nach jedem "Sofort zurueck" ist die App im Zustand *pending verify* -
   die Schleife griff also genau dann, wenn man erfahrungsgemaess ins Menue schaut.
5. Damit lief **weder** `ota_versionen_abfragen()` **noch** die Pruefung. Das Menue zeigte seine
   Platzhaltertexte, und zwar dauerhaft - kein Warten half.

Im Mitschnitt sah man es unmissverstaendlich: letzte Boot-Zeile `Start: Startbildschirm
angezeigt`, WLAN verbunden bei 5,9 s, danach **148 Sekunden voellige Stille**. In der
Sitzung davor dasselbe ueber 134 Sekunden.

**Der gefaehrlichere Teil:** Dieselbe Funktion ruft nach `OTA_BEWAEHRUNG_MS` (10 Minuten)
`esp_ota_mark_app_invalid_rollback_and_reboot()` auf. Eine voellig gesunde Firmware waere also
automatisch zurueckgenommen worden - nur weil jemand im Menue nachgesehen hat, ob das Update
angekommen ist. Da das Zurueckschalten die App erneut auf *pending verify* setzt, haette das
sogar hin- und herspringen koennen.

**Loesung, zwei unabhaengige Aenderungen:**

1. **Entkoppelt.** Die Probe blockiert nichts mehr: `bewaehrung_beginnen()` stellt beim
   Task-Start nur fest, ob eine Probe ansteht, `bewaehrung_fortschreiben()` schreibt sie in den
   ohnehin vorhandenen Warteschleifen der Hauptschleife fort. Versionsliste und Update-Pruefung
   laufen unabhaengig davon.
2. **Frist pausiert, solange sie nicht fair laufen kann.** Neu `kalender_task_laeuft()`
   (kalender_anzeige.h): Zeigt es false, wurde der Task noch gar nicht erzeugt - der Kalender
   KANN dann nicht laden, und die Uhr steht still. Bewusst an den Task gekoppelt und nicht an
   "Menue offen": das ist die ehrliche Bedingung ("Kriterium nicht pruefbar") und gilt
   unabhaengig davon, warum der Ablauf haengt. `s_einstellungen_status` waere als Signal ohnehin
   untauglich gewesen - es ist statisch mit `EINRICHTUNG_OFFEN` vorbelegt, also schon vor dem
   ersten Oeffnen "offen".

**Das Sicherheitsnetz bleibt dabei intakt** - wichtig, weil es die zentrale Absicherung fuer ein
Geraet weit entfernt ist: Laeuft der Boot-Ablauf weiter (spaetestens nach den
60-Sekunden-Timeouts der Boot-Phasen), laeuft auch die Frist weiter. Und stuerzt die neue
Firmware ab oder haengt sie, startet das Geraet neu, ohne bestaetigt zu haben - dann nimmt schon
der Bootloader die vorherige Version zurueck, ganz ohne diese Frist.

**Zwei Lehren:**

- **Peters "!!!" ernst nehmen.** Seine Beobachtung war praezise richtig, meine bequeme Erklaerung
  ("zu schnell geklickt") war es nicht. Dasselbe Muster wie beim Wochentag-Button-Bug (#19) und
  beim abendlichen Abdunkeln (#28) - hier zum dritten Mal.
- **Was blockierend wartet, blockiert mehr als man denkt.** Eine Warteschleife am Anfang eines
  Tasks legt ALLES lahm, was dieser Task sonst noch tut. Diese Codebasis hat dasselbe Muster
  schon in #37 gehabt (schnelle Abfrage hinter langsamer in derselben Schlange).

---

## 42. Hauptanzeige blitzte beim Booten ungedimmt auf, bevor sie eingeblendet wurde

**Problem:** Peters Beobachtung: "Beim Starten werden die 3 Symbole angezeigt und anschliessend
kurz der Hauptbildschirm, danach dunkel und der Hauptbildschirm wird wieder eingeblendet." Die
fertige Hauptanzeige war also einmal kurz zu sehen, verschwand wieder und kam dann per
Einblend-Animation zurueck.

**Ursache:** `app_main()` setzt das Dimm-Overlay vor dem Bildschirmwechsel bewusst auf
`LV_OPA_COVER` (schwarz), damit die Hauptanzeige nicht mit halbfertigen Werten aufblitzt - und
ruft direkt danach `uhr_tick(NULL)`, um die echten Farben/Texte zu setzen. Der Kommentar dort
versprach "unsichtbar, da das Overlay noch komplett deckt". Das stimmte nicht: `uhr_tick()`
laeuft beim ALLERERSTEN Aufruf garantiert durch `modus_anwenden()` (der erste Aufruf zaehlt dort
immer als Moduswechsel - absichtlich, damit die Status-Symbole ihre Farbe bekommen), und
`modus_anwenden()` setzt das Overlay auf die Deckkraft des aktuellen Modus, tagsueber also auf
`LV_OPA_TRANSP`. Die Absicherung hob sich damit selbst auf.

**Messung statt Vermutung:** Zwei temporaere Log-Zeilen (in `modus_anwenden()` und direkt vor dem
Animationsstart) zeigten die Luecke exakt: Overlay-Deckkraft `0` gesetzt bei `t=16198032 us`,
beim Animationsstart immer noch `0` bei `t=16312680 us` - **114,6 ms** ungedimmt sichtbar, bei
der Bildrate des Panels mehrere vollstaendig gezeichnete Frames.

**Loesung:** `uhr_tick(NULL)`, das Zuruecksetzen des Overlays auf `LV_OPA_COVER` und der
Animationsstart laufen jetzt unter EINEM durchgehenden `lvgl_port_lock()` - der LVGL-Task
zeichnet dazwischen keinen einzigen Frame. Der verschachtelte Lock in `modus_anwenden()` ist
unproblematisch, weil `lvgl_port_lock()` auf `xSemaphoreTakeRecursive` aufsetzt (nachgesehen in
`managed_components/espressif__esp_lvgl_port/src/lvgl9/esp_lvgl_port.c`). Kontrollmessung nach
dem Fix: Deckkraft beim Animationsstart `255`, Animation weiterhin 2s, kein Deadlock.

**Lehre:** Ein Kommentar, der eine Annahme behauptet ("unsichtbar, da abgedeckt"), ist keine
Garantie - eine dazwischenliegende Funktion kann genau die Bedingung aufheben, auf die er sich
beruft. Wo eine Reihenfolge fuer die Optik zwingend ist, muss sie erzwungen werden (hier: ein
gemeinsamer Lock), nicht nur beschrieben. Und erneut: Peters Beobachtung war praezise richtig
(nach #19, #28, #41 das vierte Mal).

---

## 43. Spaete Tabletten fielen durch Mitternacht - unsichtbar, nicht abhakbar, sofort "vergessen"

**Problem:** Peters Beobachtung: "Um 1:00 Uhr sehe ich keine Checkbox bei einem Medikament, das um
23:00 Uhr eingenommen werden sollte." Dahinter steckten vier Befunde, die erst zusammen das
eigentliche Loch ergaben:

1. **Nie erinnert.** Der Nachtmodus beginnt um 22:00 (`zeit_tageszeit`), und `erinnerung_pruefen()`
   kehrt darin sofort zurueck - Peters ausdruecklicher Wunsch ("Bildschirm bleibt zwischen 22:00
   und 6:00 dunkel"). Eine 23:00-Tablette bekam damit NIE ein Erinnerungsfenster.
2. **Nicht sichtbar.** Nachts blendet `modus_anwenden()` Tabletten und Termine ganz aus.
3. **Um 00:00 verschwunden.** `fuer_heute_neu_parsen()` baut die Tagesliste neu auf; alle
   Zeitvergleiche rechnen in "Minuten seit heute 00:00". Die gestrige Tablette war weg, und die
   HEUTIGE 23:00-Tablette stand korrekt ohne Checkbox da (noch 22 Stunden hin) - genau das, was
   Peter sah.
4. **Sofort als Versaeumnis gebucht.** Das tags zuvor eingebaute Langzeitprotokoll (#42-Nachbar,
   `tabletten_protokoll.c`) archivierte den Vortag um 00:00 mit `ist_minute = -1`. Wer um 23:55
   nahm und um 00:10 abhaken wollte, stand dauerhaft als "vergessen" in der Bilanz.

**Loesung:** Ein "schwebender Vortag" (`s_vortag` in `kalender_anzeige.c`). Beim Tageswechsel wird
der komplette Vortag dorthin gerettet statt archiviert; seine noch offenen Tabletten werden bei
JEDEM Parse-Lauf vorne in die Tagesliste eingeblendet (`vom_vortag = true`) und bleiben abhakbar.
Erst ab `KALENDER_UEBERHANG_ENDE_STUNDE` (04:00, Peters Wahl) wandert der Tag ins Protokoll und
verschwindet aus der Anzeige.

Der Kern ist die Zeitrechnung: `kalender_tablette_soll_minute()` versetzt einen Vortags-Eintrag um
einen ganzen Tag zurueck. Ohne das galt eine 23:00-Tablette um 01:00 als "in 22 Stunden faellig"
(ZUKUNFT) - also weiterhin ohne Checkbox. Auf dem Geraet mit temporaerer Instrumentierung
geprueft, alle drei Faelle wie erwartet:

    Vortag 23:00,       jetzt 01:00 -> soll=-60  ende=0    status=UEBERFAELLIG
    Heute  23:00,       jetzt 01:00 -> soll=1380 ende=1440 status=ZUKUNFT
    Vortag 23:00-23:30, jetzt 00:10 -> ende=-30           status=UEBERFAELLIG

Zusaetzlich bleibt die Tabletten-Spalte nachts sichtbar, solange etwas offen ist - ohne Popup und
ohne Aufhellen, damit die Nachtruhe erhalten bleibt (Peters Entscheidung: "Liste ja, Fenster
nein").

**Zwei Fallen beim Nachbauen:**

- Die Bestaetigung eines nachhaengenden Eintrags muss an den schwebenden Vortag zurueckgeschrieben
  werden. Sonst ist sie beim naechsten Kalender-Abruf (alle 15 min, baut die Tagesliste neu auf)
  wieder verschwunden und fehlt spaeter im Protokoll.
- Nachhaengende Eintraege duerfen NICHT in `tablette.txt` landen: die Datei gilt fuer den heutigen
  Tagesschluessel, der Eintrag stammt aber von gestern - nach einem Neustart erschiene die
  gestrige Tablette sonst als heute bereits genommen.

**Lehre:** "Der Tag wechselt um Mitternacht" ist eine Kalender-Wahrheit, keine menschliche. Wo
eine Anzeige dem Tagesrhythmus von Menschen folgen soll, braucht die Tagesgrenze einen Uebergang -
und solange der laeuft, darf noch kein Urteil ueber den vergangenen Tag gefaellt werden.

---

## 44. LV_LABEL_LONG_DOT ohne feste Hoehe bricht um, statt abzuschneiden

**Problem:** Im Tagesfenster liefen die Tabletten-Zeilen waagerecht in die Termine-Spalte hinein
("Paracetamol" lag auf "Keine Termine.", im Screenshot gesehen). Naheliegender Fix: Breite
begrenzen und `LV_LABEL_LONG_DOT` setzen, wie es das Heute-Fenster laengst tut. Danach war es
schlimmer - die Zeile brach in zwei Zeilen um und die zweite lag ueber der naechsten Tablette.

**Ursache:** `LV_LABEL_LONG_DOT` heisst nicht "in einer Zeile abschneiden", sondern "die Groesse
des Objekts beibehalten und in der LETZTEN sichtbaren Zeile Punkte setzen". Ist nur die Breite
gesetzt und die Hoehe bleibt automatisch, waechst das Label eben nach unten: es bricht um, und
Punkte erscheinen nie, weil alles hineinpasst. Die Zeilen des Tagesfensters werden mit festem
Y-Abstand positioniert (`y += 36`), das umgebrochene Label deckt also seinen Nachbarn zu.

**Loesung:** `lv_obj_set_size(label, BREITE, HOEHE)` - beides. Erst die Hoehenbegrenzung auf eine
Zeile erzwingt das gewuenschte "eine Zeile mit ...". Reihenfolge wie im Heute-Fenster:
`lv_label_set_long_mode()` und `lv_obj_set_size()` VOR `lv_label_set_text()`.

**Lehre:** Der Name des Modus beschreibt das Ergebnis nur zusammen mit der Objektgroesse. Bei
allem, was in einer Spalte oder Tabelle steht, immer Breite UND Hoehe setzen - sonst verschiebt
sich der Fehler nur von "laeuft nach rechts raus" zu "deckt die naechste Zeile zu". Beide
Zustaende sahen im seriellen Log identisch aus; sichtbar wurden sie erst im Screenshot.

---

## 45. "Montserrat hat keine Symbolglyphen" stimmte - der Schluss daraus nicht

**Problem:** Abgehakte Tabletten waren jahrelang mit dem ASCII-Ersatz `[x] ` markiert. Begruendet
war das mit einem gepruefen Befund: `lv_font_conv --range 0x2713` bricht bei Montserrat-Bold mit
"doesn't have any characters included in range" ab, die Schrift enthaelt schlicht keinen Haken.
Daraus war geschlossen worden, ein echtes Hakenzeichen sei nicht moeglich.

**Ursache des Irrtums:** Der Befund galt fuer EINE Schrift, die Schlussfolgerung fuer das
Werkzeug. `lv_font_conv` nimmt aber mehrere `--font`-Argumente, jedes mit eigenem `--range`, und
mischt sie in eine einzige Ausgabedatei. Die fehlende Glyphe muss also nicht in Montserrat
stecken - sie muss nur irgendwo herkommen.

**Loesung:** Zweite Quelle nur fuer diese eine Glyphe: Noto Sans Symbols 2 (wie Montserrat unter
der SIL Open Font License 1.1, die Lizenzlage bleibt damit unveraendert). Genommen wurde U+2714
(kraeftig) statt U+2713 (duenn) - beide Varianten wurden als Bild nebeneinandergelegt, neben
Montserrat Bold wirkt der duenne wie ein Fremdkoerper. Im Quelltext steht der Haken als
`"\xE2\x9C\x94 "`, damit die Dateien ASCII bleiben; das Leerzeichen dahinter beendet zugleich
die Hex-Escape-Folge, die sonst jede weitere Hex-Ziffer mitfraesse.

**Nebeneffekt:** Der Haken ist bei 28 px 26,0 px breit, `[x]` war 37,3 px - lange Tablettennamen
werden dadurch etwas spaeter abgeschnitten. Gemessen ueber `lv_font_conv --format dump`, das die
Glyphen als PNG samt Vorschubbreiten ausgibt; damit laesst sich eine Schriftaenderung ansehen und
nachmessen, bevor sie auf dem Geraet landet.

**Lehre:** Ein negativer Befund zu einer Zutat ist kein negativer Befund zum Rezept. Wenn eine
Begruendung in der Doku steht ("geht nicht, weil X keine Y enthaelt"), lohnt die Frage, ob sie
das Werkzeug wirklich ausschoepft - hier lag zwischen "unmoeglich" und "geht" ein einziges
zusaetzliches Kommandozeilen-Argument.
