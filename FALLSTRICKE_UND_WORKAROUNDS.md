# Entwicklungs-Logbuch: Gelöste Fallstricke & Workarounds

Dieses Dokument dokumentiert die technischen Hürden, die während der Entwicklung der Seniorenuhr aufgetreten sind, sowie die erarbeiteten Lösungen und Best Practices.

---

## 1. ESP-IDF `install.ps1`/`export.ps1` verweigern den Dienst innerhalb einer aktiven Python-Virtualenv

**Problem:** Sowohl die Werkzeug-Installation (`install.ps1 esp32s3`) als auch das spätere Aktivieren der Umgebung (`export.ps1`) brachen mit `ERROR: This script was called from a virtual environment, can not create a virtual environment again` ab.
**Ursache:** Die PowerShell-Sitzung dieser Maschine startet mit einer bereits aktivierten Python-Virtualenv (`$env:VIRTUAL_ENV` zeigt auf ein fremdes venv, z. B. eines Hilfswerkzeugs). ESP-IDFs eigenes Setup will selbst ein venv unter `~/.espressif` anlegen und bricht ab, sobald es eine bereits aktive Virtualenv erkennt.
**Lösung:** Vor jedem `install.ps1`/`export.ps1`-Aufruf `Remove-Item Env:VIRTUAL_ENV -ErrorAction SilentlyContinue` ausführen. Gilt für **jede neue Shell-Sitzung**, die mit ESP-IDF arbeitet — steht auch in `ENTWICKLUNG.md`.

---

## 2. Board bootete in einer Assert-Schleife — Flash-Chip ist 8 MB, nicht 16 MB wie beworben

**Problem:** Nach dem ersten Flash-Vorgang mit `sdkconfig.defaults` auf 16 MB Flash bootete das Board endlos mit `assert failed: __esp_system_init_fn_init_flash startup_funcs.c:118 (flash_ret == ESP_OK)`.
**Ursache:** Die Waveshare-Produktseite nennt 16 MB Flash für das ESP32-S3-Touch-LCD-7 — das konkret gelieferte Board ist aber die **N8R8-Variante mit 8 MB Flash** (verifiziert per `python -m esptool --chip esp32s3 -p COM3 flash_id`: GigaDevice-Chip, 8 MB, Quad-Modus). Die Firmware versuchte, eine Flash-Größe zu adressieren, die physisch nicht vorhanden ist.
**Lösung:** `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y` statt `_16MB` in `sdkconfig.defaults`, Partitionstabelle (`partitions.csv`) entsprechend auf 8 MB angepasst, `sdkconfig` gelöscht und neu erzeugt.
**Lehre:** Bei Waveshare-Boards (und generell bei Klonen/Werksvarianten) die tatsächliche Chip-Bestückung immer per `esptool flash_id` gegenprüfen, statt der Produktseite blind zu vertrauen — insbesondere bevor die Flash-Größen-Konfiguration gesetzt wird.

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

## 13. Anzeige fiel waehrend der Tabletten-Bedienung in den Nacht-Modus zurueck — LVGL-Presses bubbeln nicht automatisch zum Screen

**Problem:** Peter berichtete nach einem laengeren Praxis-Test, dass die Anzeige beim Bedienen des Heute-Fensters (Tabletten abhaken) mitten in der Interaktion in den dunklen Abend-/Nacht-Modus zurueckfiel, obwohl die Beruehrungs-Wachzeit (`BERUEHRUNG_WACHZEIT_US`, 30s) eigentlich genau das verhindern soll.
**Ursache:** `beruehrung_callback` haengt nur an `s_bildschirm` selbst (`LV_EVENT_PRESSED`). Die Tages-/Heute-Fenster von `tagesansicht.c` sind aber eigene, klickbare `lv_obj_create(s_scr)`-Panels, die als Overlay ÜBER dem Hauptbildschirm sitzen. LVGL leitet `LV_EVENT_PRESSED` standardmaessig NICHT automatisch an Vorfahren-Objekte weiter (kein Bubbling ohne `LV_OBJ_FLAG_EVENT_BUBBLE` auf dem jeweiligen Kind) - jeder Tipp auf einen Schieberegler, den Schliessen-Button oder auch nur den Fensterhintergrund wird also vom Fenster-Panel selbst "geschluckt" und erreicht `s_bildschirm`s Callback nie. Sobald ein solches Fenster offen ist, verlaengert keine Interaktion darin mehr die 30s-Wachzeit.
**Loesung:** `aktueller_modus()` (app_main.c) prueft zusaetzlich `tagesansicht_fenster_offen()` (neue Abfrage in tagesansicht.c, `true` solange `s_tages_fenster`/`s_heute_fenster` existieren) und erzwingt Tag-Modus, solange irgendein Tages-/Heute-Fenster offen ist - unabhaengig vom Ablauf der 30s-Wachzeit.
**Lehre:** Ein `LV_EVENT_PRESSED`-Handler auf einem Screen/Root-Objekt faengt NUR Presses auf nicht-klickbare Kinder ab (z. B. reine `lv_label`s, bei denen der Hit-Test bis zum naechsten klickbaren Vorfahren "durchfaellt"). Sobald ein klickbares Overlay (Fenster, Popup, Keyboard, ...) darueber liegt, sieht der Root-Handler von Interaktionen darin nichts mehr - fuer sowas immer eine explizite Zustandsabfrage ("ist gerade ein Overlay offen?") statt sich auf Event-Bubbling zu verlassen.
