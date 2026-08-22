/*
 * protokoll_ansicht.h — bereitet einen aufgezeichneten Tag fuer die
 * Tagesansicht auf.
 *
 * Hintergrund: Das Tagesfenster eines vergangenen Tages zeigte bisher, was
 * laut Kalender ANSTAND. Was tatsaechlich genommen wurde, steht seit dem
 * Tageswechsel aber im Langzeitprotokoll (tabletten_protokoll.h) - der
 * Bestaetigungsstand der Kalenderschicht gilt immer nur fuer heute. Wer
 * gestern nachschaute, sah deshalb einen Plan und keine Tatsachen.
 *
 * Diese Datei uebersetzt Protokoll-Eintraege in die Anzeige-Struktur der
 * Tagesansicht. Sie enthaelt bewusst nur reine Logik und keinen LVGL-Aufruf:
 * damit laesst sie sich auf dem PC pruefen (test_host/test_protokoll.c),
 * statt auf einen echten Tageswechsel auf dem Geraet zu warten.
 */
#ifndef PROTOKOLL_ANSICHT_H
#define PROTOKOLL_ANSICHT_H

#include "kalender_anzeige.h"
#include "tabletten_protokoll.h"

/* Wandelt die Protokoll-Eintraege eines Tages in Anzeige-Eintraege um und
 * bewertet jeden davon.
 *
 * `eintraege` und `zustaende` fassen je `max` Elemente und werden parallel
 * gefuellt: zu eintraege[i] gehoert zustaende[i]. Rueckgabe ist die Anzahl
 * belegter Plaetze (0, wenn nichts aufgezeichnet ist oder ein Zeiger fehlt).
 *
 * Die Bewertung kommt aus tabletten_protokoll_zustand() und wird hier NICHT
 * nachgebaut - Rueckblick und Tagesfenster sollen dasselbe Urteil zeigen. */
int protokoll_ansicht_aufbereiten(const tabletten_protokoll_eintrag_t *quelle, int anzahl,
                                  kalender_tag_eintrag_t *eintraege,
                                  tabletten_zustand_t *zustaende, int max);

#endif /* PROTOKOLL_ANSICHT_H */
