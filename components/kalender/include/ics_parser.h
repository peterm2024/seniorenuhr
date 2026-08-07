/*
 * ics_parser.h — Mini-Parser für iCalendar-Dateien (ICS), z. B. aus Google Kalender.
 *
 * Bewusst klein gehalten: Er beantwortet genau eine Frage —
 * "Welche Termine und Tabletten stehen an einem bestimmten Tag an?"
 *
 * Unterstützt:
 *   - Einzeltermine (mit Uhrzeit oder ganztägig)
 *   - Wiederholungen (RRULE): FREQ=DAILY / WEEKLY (mit BYDAY) / MONTHLY / YEARLY,
 *     jeweils mit INTERVAL, COUNT und UNTIL
 *   - Ausnahmen (EXDATE), abgesagte Termine (STATUS:CANCELLED)
 *   - Zeilen-Faltung, UTF-8, Escape-Sequenzen im Titel
 *   - Zeitangaben mit TZID (als lokale Zeit interpretiert) und in UTC ("Z",
 *     wird nach Europe/Berlin inkl. Sommerzeit umgerechnet)
 *
 * Tabletten werden als Termine mit dem Titel-Präfix "TABLETTE:" gepflegt
 * (Groß-/Kleinschreibung egal). Das Präfix wird im Ergebnis entfernt und
 * das Feld ist_tablette gesetzt.
 *
 * Portabel: reines C (C99), keine ESP- oder OS-Abhängigkeiten — läuft für
 * Tests auf dem PC und unverändert auf dem ESP32-S3.
 */
#ifndef ICS_PARSER_H
#define ICS_PARSER_H

#include <stdbool.h>
#include <stddef.h>

#define ICS_TITEL_MAX 96

typedef struct {
    int jahr;    /* z. B. 2026 */
    int monat;   /* 1..12 */
    int tag;     /* 1..31 */
    int stunde;  /* 0..23, bei ganztägigen Terminen 0 */
    int minute;  /* 0..59 */
} ics_zeit_t;

/* Reicht für kurze Hinweise wie "nüchtern" oder "mit reichlich Wasser",
 * nicht für lange ICS-Freitexte - passend zum Projekt-Prinzip "abschneiden
 * statt umbrechen" in der Anzeige. */
#define ICS_BESCHREIBUNG_MAX 128

typedef struct {
    char       titel[ICS_TITEL_MAX];       /* UTF-8; ohne "TABLETTE:"-Präfix */
    char       beschreibung[ICS_BESCHREIBUNG_MAX]; /* UTF-8; leer, wenn nicht gesetzt */
    ics_zeit_t beginn;                     /* Beginn am angefragten Tag (lokale Zeit) */
    ics_zeit_t ende;                       /* nur gültig, wenn hat_ende true ist */
    bool       hat_ende;                   /* true nur bei einer echten DTEND-Uhrzeit
                                             * (nicht bei ganztägigen Terminen) */
    bool       ganztags;
    bool       ist_tablette;
} ics_termin_t;

/*
 * Sucht alle Termine, die am angegebenen Tag (lokale Zeit Europe/Berlin)
 * stattfinden, und schreibt sie sortiert in `ergebnis`:
 * zuerst ganztägige Einträge, dann nach Uhrzeit aufsteigend.
 *
 * ics_text muss nicht nullterminiert sein; laenge zählt die Bytes.
 * Rückgabe: Anzahl gefundener Termine (0..max_ergebnisse),
 *           -1 bei ungültigen Argumenten.
 */
int ics_termine_fuer_tag(const char *ics_text, size_t laenge,
                         int jahr, int monat, int tag,
                         ics_termin_t *ergebnis, int max_ergebnisse);

#endif /* ICS_PARSER_H */
