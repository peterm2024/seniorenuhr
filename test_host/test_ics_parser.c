/*
 * test_ics_parser.c — Tests für den ICS-Parser, laufen auf dem PC.
 *
 * Bauen & starten:  test_host\teste.ps1
 * (oder von Hand:   gcc -Wall -Wextra -I../components/kalender/include
 *                       ../components/kalender/ics_parser.c
 *                       test_ics_parser.c -o test_ics && ./test_ics)
 */
#include <stdio.h>
#include <string.h>

#include "ics_parser.h"

static int fehler = 0;
static int geprueft = 0;

#define PRUEFE(bedingung, beschreibung)                                   \
    do {                                                                  \
        geprueft++;                                                       \
        if (bedingung) {                                                  \
            printf("  ok   - %s\n", beschreibung);                        \
        } else {                                                          \
            printf("  FEHLT- %s (Zeile %d)\n", beschreibung, __LINE__);   \
            fehler++;                                                     \
        }                                                                 \
    } while (0)

/* Beispiel-Kalender, wie ihn Google exportiert (CRLF, gefaltete Zeilen). */
static const char KALENDER[] =
    "BEGIN:VCALENDAR\r\n"
    "PRODID:-//Google Inc//Google Calendar 70.9054//EN\r\n"
    "VERSION:2.0\r\n"

    /* 1) Einzeltermin mit TZID am Mi 15.07.2026, 15:00 */
    "BEGIN:VEVENT\r\n"
    "DTSTART;TZID=Europe/Berlin:20260715T150000\r\n"
    "DTEND;TZID=Europe/Berlin:20260715T154500\r\n"
    "SUMMARY:Dr. Müller (Hausarzt)\r\n"
    "DESCRIPTION:Blutdruck vorher messen\r\n"
    "BEGIN:VALARM\r\n"
    "ACTION:DISPLAY\r\n"
    "SUMMARY:Alarm-Text der NICHT angezeigt werden darf\r\n"
    "END:VALARM\r\n"
    "END:VEVENT\r\n"

    /* 2) Tablette, täglich 08:00 seit 01.06.2026, mit Beschreibung, ohne DTEND */
    "BEGIN:VEVENT\r\n"
    "DTSTART;TZID=Europe/Berlin:20260601T080000\r\n"
    "RRULE:FREQ=DAILY\r\n"
    "SUMMARY:TABLETTE: Blutdruck (1 morgens)\r\n"
    "DESCRIPTION:nüchtern\r\n"
    "END:VEVENT\r\n"

    /* 3) Tablette Mo+Do 18:00, mit Ausnahme am Do 16.07.2026 */
    "BEGIN:VEVENT\r\n"
    "DTSTART;TZID=Europe/Berlin:20260504T180000\r\n"
    "RRULE:FREQ=WEEKLY;BYDAY=MO,TH\r\n"
    "EXDATE;TZID=Europe/Berlin:20260716T180000\r\n"
    "SUMMARY:Tablette: Herz (abends)\r\n"
    "END:VEVENT\r\n"

    /* 4) Ganztägig: Geburtstag am 20.07.2026, jährlich */
    "BEGIN:VEVENT\r\n"
    "DTSTART;VALUE=DATE:19360720\r\n"
    "DTEND;VALUE=DATE:19360721\r\n"
    "RRULE:FREQ=YEARLY\r\n"
    "SUMMARY:Geburtstag Anna\r\n"
    "END:VEVENT\r\n"

    /* 5) Abgelaufene Wiederholung (UNTIL 30.06.2026) */
    "BEGIN:VEVENT\r\n"
    "DTSTART;TZID=Europe/Berlin:20260401T100000\r\n"
    "RRULE:FREQ=WEEKLY;BYDAY=WE;UNTIL=20260630T215959Z\r\n"
    "SUMMARY:Krankengymnastik (alt)\r\n"
    "END:VEVENT\r\n"

    /* 6) UTC-Zeit: 13:00Z = 15:00 Sommerzeit, gefaltete Zeile mit Umlaut */
    "BEGIN:VEVENT\r\n"
    "DTSTART:20260716T130000Z\r\n"
    "SUMMARY:Kaffee mit Frau Schrö\r\n"
    " der\r\n"
    "END:VEVENT\r\n"

    /* 7) COUNT=3 täglich ab 01.07.2026 -> letzter Termin am 03.07. */
    "BEGIN:VEVENT\r\n"
    "DTSTART;TZID=Europe/Berlin:20260701T090000\r\n"
    "RRULE:FREQ=DAILY;COUNT=3\r\n"
    "SUMMARY:Blutabnahme-Serie\r\n"
    "END:VEVENT\r\n"

    /* 8) Abgesagter Termin am 15.07.2026 */
    "BEGIN:VEVENT\r\n"
    "DTSTART;TZID=Europe/Berlin:20260715T110000\r\n"
    "STATUS:CANCELLED\r\n"
    "SUMMARY:Abgesagter Friseur\r\n"
    "END:VEVENT\r\n"

    /* 9) Alle 2 Wochen dienstags ab 07.07.2026 (14.07. fällt aus, 21.07. nicht) */
    "BEGIN:VEVENT\r\n"
    "DTSTART;TZID=Europe/Berlin:20260707T140000\r\n"
    "RRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=TU\r\n"
    "SUMMARY:Fußpflege\r\n"
    "END:VEVENT\r\n"

    /* 10) Titel mit maskiertem Komma am 15.07.2026 */
    "BEGIN:VEVENT\r\n"
    "DTSTART;TZID=Europe/Berlin:20260715T083000\r\n"
    "SUMMARY:Einkaufen: Brot\\, Milch\r\n"
    "END:VEVENT\r\n"

    "END:VCALENDAR\r\n";

static int suche(ics_termin_t *t, int n, const char *titel)
{
    for (int i = 0; i < n; i++)
        if (strcmp(t[i].titel, titel) == 0) return i;
    return -1;
}

int main(void)
{
    ics_termin_t t[16];
    size_t len = sizeof KALENDER - 1;
    int n;

    printf("== Mittwoch, 15.07.2026 ==\n");
    n = ics_termine_fuer_tag(KALENDER, len, 2026, 7, 15, t, 16);
    PRUEFE(n == 3, "drei Eintraege am 15.07. (Tablette, Einkauf, Arzt)");
    int arzt = suche(t, n, "Dr. Müller (Hausarzt)");
    PRUEFE(arzt >= 0, "Einzeltermin 'Dr. Müller (Hausarzt)' gefunden");
    PRUEFE(arzt >= 0 && t[arzt].beginn.stunde == 15 && t[arzt].beginn.minute == 0,
           "Arzttermin beginnt 15:00");
    PRUEFE(arzt >= 0 && !t[arzt].ist_tablette, "Arzttermin ist keine Tablette");
    PRUEFE(arzt >= 0 && t[arzt].hat_ende && t[arzt].ende.stunde == 15 && t[arzt].ende.minute == 45,
           "Arzttermin: DTEND als Endzeit uebernommen (15:45)");
    PRUEFE(arzt >= 0 && strcmp(t[arzt].beschreibung, "Blutdruck vorher messen") == 0,
           "Arzttermin: Beschreibung uebernommen");
    int blut = suche(t, n, "Blutdruck (1 morgens)");
    PRUEFE(blut >= 0, "taegliche Tablette gefunden, Praefix entfernt");
    PRUEFE(blut >= 0 && t[blut].ist_tablette, "als Tablette erkannt");
    PRUEFE(blut >= 0 && t[blut].beginn.stunde == 8, "Tablette um 08:00");
    PRUEFE(blut >= 0 && !t[blut].hat_ende, "Tablette ohne DTEND: hat_ende bleibt false");
    PRUEFE(blut >= 0 && strcmp(t[blut].beschreibung, "nüchtern") == 0,
           "Tablette: Beschreibung 'nuechtern' uebernommen");
    PRUEFE(suche(t, n, "Einkaufen: Brot, Milch") >= 0,
           "maskiertes Komma im Titel aufgeloest");
    PRUEFE(suche(t, n, "Abgesagter Friseur") < 0, "abgesagter Termin fehlt");
    PRUEFE(suche(t, n, "Krankengymnastik (alt)") < 0,
           "UNTIL abgelaufen: Krankengymnastik fehlt (15.07. ist Mittwoch)");
    /* Sortierung: 08:00 Tablette vor 08:30 Einkauf vor 15:00 Arzt */
    PRUEFE(n == 3 && t[0].beginn.stunde * 60 + t[0].beginn.minute <=
                     t[n-1].beginn.stunde * 60 + t[n-1].beginn.minute,
           "Ergebnis nach Uhrzeit sortiert");

    printf("== Donnerstag, 16.07.2026 (EXDATE fuer Herz-Tablette) ==\n");
    n = ics_termine_fuer_tag(KALENDER, len, 2026, 7, 16, t, 16);
    PRUEFE(suche(t, n, "Herz (abends)") < 0, "EXDATE: Herz-Tablette am 16.07. fehlt");
    int kaffee = suche(t, n, "Kaffee mit Frau Schröder");
    PRUEFE(kaffee >= 0, "gefaltete Zeile mit Umlaut korrekt zusammengesetzt");
    PRUEFE(kaffee >= 0 && t[kaffee].beginn.stunde == 15,
           "13:00 UTC wurde zu 15:00 Sommerzeit");
    PRUEFE(kaffee >= 0 && t[kaffee].beschreibung[0] == '\0',
           "Kaffee-Termin ohne DESCRIPTION: Beschreibung bleibt leer");

    printf("== Montag, 20.07.2026 ==\n");
    n = ics_termine_fuer_tag(KALENDER, len, 2026, 7, 20, t, 16);
    int geb = suche(t, n, "Geburtstag Anna");
    PRUEFE(geb >= 0, "jaehrlicher Geburtstag (seit 1936) am 20.07. gefunden");
    PRUEFE(geb >= 0 && t[geb].ganztags, "Geburtstag ist ganztaegig");
    PRUEFE(geb >= 0 && geb == 0, "ganztaegiger Eintrag steht ganz oben");
    PRUEFE(suche(t, n, "Herz (abends)") >= 0, "Herz-Tablette am Montag vorhanden");

    printf("== COUNT-Grenze: 03.07. ja, 04.07. nein ==\n");
    n = ics_termine_fuer_tag(KALENDER, len, 2026, 7, 3, t, 16);
    PRUEFE(suche(t, n, "Blutabnahme-Serie") >= 0, "COUNT=3: dritter Termin am 03.07.");
    n = ics_termine_fuer_tag(KALENDER, len, 2026, 7, 4, t, 16);
    PRUEFE(suche(t, n, "Blutabnahme-Serie") < 0, "COUNT=3: kein Termin am 04.07.");

    printf("== INTERVAL=2: 14.07. nein, 21.07. ja ==\n");
    n = ics_termine_fuer_tag(KALENDER, len, 2026, 7, 14, t, 16);
    PRUEFE(suche(t, n, "Fußpflege") < 0, "zweiwoechentlich: 14.07. faellt aus");
    n = ics_termine_fuer_tag(KALENDER, len, 2026, 7, 21, t, 16);
    PRUEFE(suche(t, n, "Fußpflege") >= 0, "zweiwoechentlich: 21.07. findet statt");

    printf("== Winterzeit: 13:00 UTC im Dezember = 14:00 MEZ ==\n");
    static const char WINTER[] =
        "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\n"
        "DTSTART:20261210T130000Z\r\n"
        "SUMMARY:Wintertermin\r\n"
        "END:VEVENT\r\nEND:VCALENDAR\r\n";
    n = ics_termine_fuer_tag(WINTER, sizeof WINTER - 1, 2026, 12, 10, t, 16);
    PRUEFE(n == 1 && t[0].beginn.stunde == 14, "13:00Z -> 14:00 MEZ");

    printf("== Randfaelle ==\n");
    n = ics_termine_fuer_tag(KALENDER, len, 2026, 13, 1, t, 16);
    PRUEFE(n == -1, "unsinniger Monat wird abgewiesen");
    n = ics_termine_fuer_tag("kein ics", 8, 2026, 7, 15, t, 16);
    PRUEFE(n == 0, "Nicht-ICS-Text ergibt leere Liste");

    printf("\n%d Pruefungen, %d Fehler\n", geprueft, fehler);
    return fehler ? 1 : 0;
}
