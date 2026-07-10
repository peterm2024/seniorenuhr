/*
 * zeige_tag.c — Kommandozeilen-Werkzeug: zeigt, was die Seniorenuhr an
 * einem bestimmten Tag anzeigen würde. Praktisch, um den Parser gegen
 * den echten Google-Kalender zu testen, bevor die Hardware da ist.
 *
 * Aufruf:  zeige_tag <datei.ics> [JJJJ-MM-TT]
 *          (ohne Datum: heute)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ics_parser.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Aufruf: %s <datei.ics> [JJJJ-MM-TT]\n", argv[0]);
        return 2;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "Kann '%s' nicht oeffnen\n", argv[1]);
        return 2;
    }
    fseek(f, 0, SEEK_END);
    long groesse = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (groesse <= 0 || groesse > 8L * 1024 * 1024) {
        fprintf(stderr, "Unerwartete Dateigroesse (%ld Bytes)\n", groesse);
        fclose(f);
        return 2;
    }
    char *inhalt = malloc((size_t)groesse);
    if (!inhalt || fread(inhalt, 1, (size_t)groesse, f) != (size_t)groesse) {
        fprintf(stderr, "Lesefehler\n");
        fclose(f);
        return 2;
    }
    fclose(f);

    int jahr, monat, tag;
    if (argc >= 3) {
        if (sscanf(argv[2], "%d-%d-%d", &jahr, &monat, &tag) != 3) {
            fprintf(stderr, "Datum bitte als JJJJ-MM-TT\n");
            return 2;
        }
    } else {
        time_t jetzt = time(NULL);
        struct tm *lokal = localtime(&jetzt);
        jahr = lokal->tm_year + 1900;
        monat = lokal->tm_mon + 1;
        tag = lokal->tm_mday;
    }

    ics_termin_t termine[32];
    int n = ics_termine_fuer_tag(inhalt, (size_t)groesse,
                                 jahr, monat, tag, termine, 32);
    free(inhalt);
    if (n < 0) {
        fprintf(stderr, "Parser meldet ungueltige Argumente\n");
        return 2;
    }

    printf("Anzeige fuer %02d.%02d.%04d:\n", tag, monat, jahr);
    if (n == 0) {
        printf("  (keine Eintraege)\n");
        return 0;
    }
    for (int i = 0; i < n; i++) {
        const ics_termin_t *t = &termine[i];
        if (t->ganztags)
            printf("  [ganztags] %s%s\n",
                   t->ist_tablette ? "TABLETTE  " : "", t->titel);
        else
            printf("  %02d:%02d  %s%s\n",
                   t->beginn.stunde, t->beginn.minute,
                   t->ist_tablette ? "TABLETTE  " : "", t->titel);
    }
    return 0;
}
