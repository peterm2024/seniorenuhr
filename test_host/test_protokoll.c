/*
 * test_protokoll.c — Tests für das Tabletten-Langzeitprotokoll
 * (main/tabletten_protokoll.c), laufen auf dem PC.
 *
 * Warum als Host-Test und nicht auf dem Gerät: das Protokoll wird
 * ausschließlich beim MITTERNACHTS-Wechsel geschrieben. Auf dem Board ließe
 * sich das nur durch Warten oder Verstellen der Uhr prüfen — die Auswertung
 * (Bilanz, Kürzen an Tagesgrenzen, Idempotenz) ist dagegen reine Datei- und
 * Rechenarbeit und hier vollständig prüfbar.
 *
 * Bauen & starten:  test_host\teste.ps1
 */
#include <stdio.h>
#include <string.h>

#include "tabletten_protokoll.h"

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

/* Muss zu den -D-Definitionen in teste.ps1 / der CI passen. */
#ifndef PROTOKOLL_PFAD
#define PROTOKOLL_PFAD "test_prot.txt"
#endif

static void protokoll_leeren(void)
{
    remove(PROTOKOLL_PFAD);
}

/* Legt einen Tag mit `anzahl` Tabletten an. `ist_versatz[i]` ist die
 * Abweichung der Bestätigung vom Fenster-ENDE: negativ = rechtzeitig,
 * 0/positiv = zu spät, INT_MIN-Ersatz (-9999) = gar nicht genommen. */
#define NICHT_GENOMMEN (-9999)

static void tag_anlegen(int tag, int anzahl, const int *ist_versatz)
{
    tabletten_protokoll_eintrag_t e[8];
    for (int i = 0; i < anzahl; i++) {
        e[i].tag_schluessel = tag;
        e[i].soll_minute = 8 * 60 + i * 60;      /* 08:00, 09:00, ... */
        e[i].ende_minute = e[i].soll_minute + 60; /* Standardfenster 60 min */
        e[i].ist_minute = (ist_versatz[i] == NICHT_GENOMMEN)
                              ? -1
                              : e[i].ende_minute + ist_versatz[i];
        snprintf(e[i].titel, sizeof e[i].titel, "Tablette %d", i + 1);
    }
    tabletten_protokoll_tag_ablegen(e, anzahl);
}

static void test_ablegen_und_lesen(void)
{
    printf("\nAblegen und wieder auslesen\n");
    protokoll_leeren();

    const int versatz[] = { -30, 5, NICHT_GENOMMEN };
    tag_anlegen(20260817, 3, versatz);

    tabletten_protokoll_eintrag_t gelesen[8];
    int n = tabletten_protokoll_tag_lesen(20260817, gelesen, 8);
    PRUEFE(n == 3, "alle drei Eintraege des Tages kommen zurueck");
    PRUEFE(strcmp(gelesen[0].titel, "Tablette 1") == 0, "Titel bleibt erhalten");
    PRUEFE(gelesen[0].soll_minute == 480, "Soll-Zeit bleibt erhalten");
    PRUEFE(gelesen[0].ende_minute == 540, "Fenster-Ende bleibt erhalten");
    PRUEFE(gelesen[0].ist_minute == 510, "rechtzeitige Bestaetigung bleibt erhalten");
    PRUEFE(gelesen[2].ist_minute == -1, "nicht genommene Tablette bleibt als -1 erkennbar");

    PRUEFE(tabletten_protokoll_kennt_tag(20260817), "aufgezeichneter Tag wird als bekannt gemeldet");
    PRUEFE(!tabletten_protokoll_kennt_tag(20260816), "nicht aufgezeichneter Tag gilt als unbekannt");

    n = tabletten_protokoll_tag_lesen(20260816, gelesen, 8);
    PRUEFE(n == 0, "fremder Tag liefert keine Eintraege");
}

static void test_idempotenz(void)
{
    printf("\nDerselbe Tag wird nicht doppelt gezaehlt\n");
    protokoll_leeren();

    const int versatz[] = { -30, -30 };
    tag_anlegen(20260817, 2, versatz);
    tag_anlegen(20260817, 2, versatz); /* z.B. Neustart kurz nach Mitternacht */

    tabletten_protokoll_eintrag_t gelesen[16];
    int n = tabletten_protokoll_tag_lesen(20260817, gelesen, 16);
    PRUEFE(n == 2, "zweites Ablegen desselben Tages wird verworfen");

    tabletten_protokoll_bilanz_t b;
    tabletten_protokoll_bilanz_ermitteln(0, &b);
    PRUEFE(b.tage == 1, "Bilanz zaehlt den Tag nur einmal");
    PRUEFE(b.gesamt == 2, "Bilanz zaehlt die Tabletten nur einmal");
}

static void test_bilanz_zaehlt_richtig(void)
{
    printf("\nBilanz unterscheidet genommen / zu spaet / vergessen\n");
    protokoll_leeren();

    /* Tag 1: puenktlich, 10 min zu spaet, gar nicht */
    const int tag1[] = { -30, 10, NICHT_GENOMMEN };
    tag_anlegen(20260815, 3, tag1);
    /* Tag 2: puenktlich, 90 min zu spaet */
    const int tag2[] = { -5, 90 };
    tag_anlegen(20260816, 2, tag2);

    tabletten_protokoll_bilanz_t b;
    tabletten_protokoll_bilanz_ermitteln(0, &b);

    PRUEFE(b.tage == 2, "zwei unterschiedliche Tage erkannt");
    PRUEFE(b.gesamt == 5, "fuenf faellige Tabletten insgesamt");
    PRUEFE(b.genommen == 2, "zwei puenktliche Einnahmen");
    PRUEFE(b.zu_spaet == 2, "zwei verspaetete Einnahmen");
    PRUEFE(b.vergessen == 1, "eine vergessene Einnahme");
    PRUEFE(b.genommen + b.zu_spaet + b.vergessen == b.gesamt,
           "die drei Kategorien ergeben zusammen die Gesamtzahl");
}

static void test_auffaellig_nur_relevantes(void)
{
    printf("\nAuffaellig sind Vergessene und deutliche Verspaetungen\n");
    protokoll_leeren();

    /* 10 min zu spaet = unauffaellig, 90 min = auffaellig, vergessen = immer */
    const int versatz[] = { 10, 90, NICHT_GENOMMEN };
    tag_anlegen(20260815, 3, versatz);

    tabletten_protokoll_bilanz_t b;
    tabletten_protokoll_bilanz_ermitteln(0, &b);

    PRUEFE(b.zu_spaet == 2, "beide Verspaetungen zaehlen in der Statistik");
    PRUEFE(b.auffaellig_gesamt == 2, "aber nur zwei gelten als auffaellig");
    PRUEFE(b.auffaellig_anzahl == 2, "beide auffaelligen sind namentlich dabei");

    int vergessene = 0, deutlich_spaet = 0, knapp_spaet = 0;
    for (int i = 0; i < b.auffaellig_anzahl; i++) {
        if (b.auffaellig[i].ist_minute < 0)
            vergessene++;
        else if (b.auffaellig[i].ist_minute - b.auffaellig[i].ende_minute >= 30)
            deutlich_spaet++;
        else
            knapp_spaet++;
    }
    PRUEFE(vergessene == 1, "die vergessene Tablette steht in der Liste");
    PRUEFE(deutlich_spaet == 1, "die deutlich verspaetete steht in der Liste");
    PRUEFE(knapp_spaet == 0, "die knapp verspaetete steht NICHT in der Liste");
}

static void test_zeitraum_filter(void)
{
    printf("\nZeitraum-Filter beruecksichtigt nur neuere Tage\n");
    protokoll_leeren();

    const int alle_puenktlich[] = { -30, -30 };
    tag_anlegen(20260701, 2, alle_puenktlich);
    tag_anlegen(20260815, 2, alle_puenktlich);
    tag_anlegen(20260816, 2, alle_puenktlich);

    tabletten_protokoll_bilanz_t b;
    tabletten_protokoll_bilanz_ermitteln(20260815, &b);
    PRUEFE(b.tage == 2, "nur die Tage ab der Grenze zaehlen");
    PRUEFE(b.gesamt == 4, "nur deren Tabletten zaehlen");

    tabletten_protokoll_bilanz_ermitteln(20260101, &b);
    PRUEFE(b.tage == 3, "mit frueherer Grenze zaehlen alle Tage");
}

static void test_leeres_protokoll(void)
{
    printf("\nOhne Aufzeichnung bleibt die Bilanz leer statt zu raten\n");
    protokoll_leeren();

    tabletten_protokoll_bilanz_t b;
    esp_err_t r = tabletten_protokoll_bilanz_ermitteln(0, &b);
    PRUEFE(r == ESP_OK, "fehlende Datei ist kein Fehler");
    PRUEFE(b.tage == 0 && b.gesamt == 0, "Bilanz ist vollstaendig genullt");
    PRUEFE(b.auffaellig_anzahl == 0, "keine Auffaelligkeiten erfunden");
    PRUEFE(!tabletten_protokoll_kennt_tag(20260817), "kein Tag gilt als bekannt");
}

static void test_beschaedigte_zeile(void)
{
    printf("\nEine beschaedigte Zeile macht nicht das ganze Protokoll unbrauchbar\n");
    protokoll_leeren();

    FILE *f = fopen(PROTOKOLL_PFAD, "wb");
    fprintf(f, "20260815\t480\t540\t500\tGute Zeile A\n");
    fprintf(f, "kaputt-ohne-tabs\n");                 /* z.B. abgebrochener Schreibvorgang */
    fprintf(f, "20260815\t540\t600\t-1\tGute Zeile B\n");
    fclose(f);

    tabletten_protokoll_bilanz_t b;
    tabletten_protokoll_bilanz_ermitteln(0, &b);
    PRUEFE(b.gesamt == 2, "die beiden lesbaren Zeilen werden ausgewertet");
    PRUEFE(b.genommen == 1 && b.vergessen == 1, "und korrekt eingeordnet");
}

static void test_kuerzen_an_tagesgrenze(void)
{
    printf("\nKuerzen wirft ganze Tage weg, nie halbe\n");
    protokoll_leeren();

    /* Weit ueber die 40-KB-Grenze schreiben: ~40 Byte je Zeile, 6 Zeilen je
     * Tag -> rund 250 Byte/Tag, also brauchen wir ein paar hundert Tage. */
    const int versatz[] = { -30, -30, -30, -30, -30, -30 };
    for (int i = 0; i < 400; i++) {
        int tag = 20250101 + i; /* nicht kalendarisch korrekt, aber streng aufsteigend */
        tag_anlegen(tag, 6, versatz);
    }

    /* Datei muss gekuerzt worden sein... */
    FILE *f = fopen(PROTOKOLL_PFAD, "rb");
    PRUEFE(f != NULL, "Protokoll existiert nach dem Kuerzen weiter");
    long groesse = 0;
    if (f) {
        fseek(f, 0, SEEK_END);
        groesse = ftell(f);
        fclose(f);
    }
    PRUEFE(groesse > 0 && groesse < 40 * 1024, "Protokoll bleibt unter der Groessengrenze");

    /* ...und der zuletzt geschriebene Tag muss vollstaendig erhalten sein. */
    tabletten_protokoll_eintrag_t gelesen[8];
    int n = tabletten_protokoll_tag_lesen(20250101 + 399, gelesen, 8);
    PRUEFE(n == 6, "der juengste Tag ist vollstaendig erhalten");

    /* Jeder noch vorhandene Tag muss vollstaendig sein - ein angeschnittener
     * Tag wuerde die Bilanz verfaelschen. */
    tabletten_protokoll_bilanz_t b;
    tabletten_protokoll_bilanz_ermitteln(0, &b);
    PRUEFE(b.tage > 0, "es sind noch Tage vorhanden");
    PRUEFE(b.gesamt == b.tage * 6, "jeder erhaltene Tag hat alle 6 Tabletten");
}

int main(void)
{
    printf("Tabletten-Protokoll\n");

    test_ablegen_und_lesen();
    test_idempotenz();
    test_bilanz_zaehlt_richtig();
    test_auffaellig_nur_relevantes();
    test_zeitraum_filter();
    test_leeres_protokoll();
    test_beschaedigte_zeile();
    test_kuerzen_an_tagesgrenze();

    protokoll_leeren();

    printf("\n%d Pruefungen, %d Fehler\n", geprueft, fehler);
    return fehler == 0 ? 0 : 1;
}
