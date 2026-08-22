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
#include "protokoll_ansicht.h"

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

/* ---- Einstufung (tabletten_protokoll_zustand) ------------------------
 *
 * Die Regel steckte frueher nur in der Bilanz. Seit sie auch das Tagesfenster
 * eines vergangenen Tages faerbt, muss sie einzeln geprueft sein - faellt sie
 * auseinander, zeigt der Rueckblick "zu spaet", wo das Fenster gruen meldet. */
static void test_zustand_einstufung(void)
{
    printf("\nEinstufung eines Eintrags\n");

    tabletten_protokoll_eintrag_t e = { 0 };
    e.tag_schluessel = 20260818;
    e.soll_minute = 9 * 60;    /* 09:00 */
    e.ende_minute = 10 * 60;   /* Fenster bis 10:00 */

    e.ist_minute = -1;
    PRUEFE(tabletten_protokoll_zustand(&e) == TABLETTEN_ZUSTAND_VERGESSEN,
           "gar nicht bestaetigt -> vergessen");

    e.ist_minute = 9 * 60 + 30;
    PRUEFE(tabletten_protokoll_zustand(&e) == TABLETTEN_ZUSTAND_GENOMMEN,
           "innerhalb des Fensters -> genommen");

    /* Grenzfall, der die Richtung der Ungleichung festnagelt: genau auf der
     * Fenstergrenze gilt als zu spaet. Ohne diesen Fall wuerde ein Wechsel
     * von ">=" auf ">" unbemerkt durchgehen. */
    e.ist_minute = 10 * 60;
    PRUEFE(tabletten_protokoll_zustand(&e) == TABLETTEN_ZUSTAND_ZU_SPAET,
           "genau auf der Fenstergrenze -> zu spaet");

    e.ist_minute = 10 * 60 - 1;
    PRUEFE(tabletten_protokoll_zustand(&e) == TABLETTEN_ZUSTAND_GENOMMEN,
           "eine Minute vor der Grenze -> genommen");

    e.ist_minute = 23 * 60;
    PRUEFE(tabletten_protokoll_zustand(&e) == TABLETTEN_ZUSTAND_ZU_SPAET,
           "lange nach dem Fenster -> zu spaet");

    /* Ohne Fenster (aeltere Aufzeichnung) darf nichts als "zu spaet" gelten -
     * sonst wuerde eine fehlende Angabe zu einem Vorwurf. */
    e.ende_minute = -1;
    e.ist_minute = 23 * 60;
    PRUEFE(tabletten_protokoll_zustand(&e) == TABLETTEN_ZUSTAND_GENOMMEN,
           "ohne Fenstergrenze -> genommen, nicht zu spaet");

    PRUEFE(tabletten_protokoll_zustand(NULL) == TABLETTEN_ZUSTAND_VERGESSEN,
           "NULL-Zeiger stuerzt nicht ab");
}

/* ---- Aufbereitung fuer die Tagesansicht ------------------------------ */
static void test_ansicht_aufbereiten(void)
{
    printf("\nAufbereitung fuer das Tagesfenster\n");

    tabletten_protokoll_eintrag_t quelle[3] = { 0 };
    quelle[0].tag_schluessel = 20260818;
    quelle[0].soll_minute = 9 * 60;        /* 09:00 */
    quelle[0].ende_minute = 10 * 60;
    quelle[0].ist_minute = 9 * 60 + 5;     /* puenktlich */
    snprintf(quelle[0].titel, ICS_TITEL_MAX, "Paracetamol");

    quelle[1].tag_schluessel = 20260818;
    quelle[1].soll_minute = 19 * 60 + 30;  /* 19:30 */
    quelle[1].ende_minute = 20 * 60;
    quelle[1].ist_minute = 21 * 60;        /* zu spaet */
    snprintf(quelle[1].titel, ICS_TITEL_MAX, "Pantoprazol");

    quelle[2].tag_schluessel = 20260818;
    quelle[2].soll_minute = -1;            /* ganztaegig */
    quelle[2].ende_minute = -1;
    quelle[2].ist_minute = -1;             /* vergessen */
    snprintf(quelle[2].titel, ICS_TITEL_MAX, "Vitamin D");

    kalender_tag_eintrag_t ziel[3];
    tabletten_zustand_t zustaende[3];
    int n = protokoll_ansicht_aufbereiten(quelle, 3, ziel, zustaende, 3);

    PRUEFE(n == 3, "alle drei Eintraege uebernommen");
    PRUEFE(ziel[0].stunde == 9 && ziel[0].minute == 0, "09:00 richtig zerlegt");
    PRUEFE(ziel[1].stunde == 19 && ziel[1].minute == 30, "19:30 richtig zerlegt");
    PRUEFE(!ziel[0].ganztags && !ziel[1].ganztags, "Eintraege mit Uhrzeit sind nicht ganztaegig");
    PRUEFE(ziel[2].ganztags, "negative Soll-Minute wird als ganztaegig gelesen");
    PRUEFE(strcmp(ziel[0].titel, "Paracetamol") == 0, "Titel uebernommen");
    PRUEFE(ziel[0].ist_tablette && ziel[1].ist_tablette && ziel[2].ist_tablette,
           "alle Eintraege sind als Tablette markiert");
    PRUEFE(ziel[0].bestaetigt && ziel[1].bestaetigt, "genommene Eintraege sind bestaetigt");
    PRUEFE(!ziel[2].bestaetigt, "nicht genommener Eintrag ist unbestaetigt");
    PRUEFE(ziel[0].bestaetigt_minute == 9 * 60 + 5, "Bestaetigungszeit uebernommen");

    /* Der eigentliche Zweck: die Bewertung muss zum Eintrag passen - und
     * zwar Platz fuer Platz, denn die Anzeige greift parallel zu. */
    PRUEFE(zustaende[0] == TABLETTEN_ZUSTAND_GENOMMEN, "Platz 0 als genommen bewertet");
    PRUEFE(zustaende[1] == TABLETTEN_ZUSTAND_ZU_SPAET, "Platz 1 als zu spaet bewertet");
    PRUEFE(zustaende[2] == TABLETTEN_ZUSTAND_VERGESSEN, "Platz 2 als vergessen bewertet");
}

/* Randfaelle, die auf dem Geraet einen Ueberlauf bedeuten wuerden. */
static void test_ansicht_grenzen(void)
{
    printf("\nAufbereitung: Grenzfaelle\n");

    tabletten_protokoll_eintrag_t quelle[3] = { 0 };
    for (int i = 0; i < 3; i++) {
        quelle[i].soll_minute = (8 + i) * 60;
        quelle[i].ende_minute = (9 + i) * 60;
        quelle[i].ist_minute = -1;
        snprintf(quelle[i].titel, ICS_TITEL_MAX, "T%d", i);
    }

    kalender_tag_eintrag_t ziel[2];
    tabletten_zustand_t zustaende[2];

    int n = protokoll_ansicht_aufbereiten(quelle, 3, ziel, zustaende, 2);
    PRUEFE(n == 2, "mehr Eintraege als Platz: es wird bei max abgeschnitten");
    PRUEFE(strcmp(ziel[1].titel, "T1") == 0, "die ersten Eintraege werden behalten");

    PRUEFE(protokoll_ansicht_aufbereiten(NULL, 3, ziel, zustaende, 2) == 0,
           "ohne Quelle: 0 Eintraege");
    PRUEFE(protokoll_ansicht_aufbereiten(quelle, 3, NULL, zustaende, 2) == 0,
           "ohne Ziel: 0 Eintraege");
    PRUEFE(protokoll_ansicht_aufbereiten(quelle, 3, ziel, NULL, 2) == 0,
           "ohne Zustandsfeld: 0 Eintraege");
    PRUEFE(protokoll_ansicht_aufbereiten(quelle, 0, ziel, zustaende, 2) == 0,
           "leere Quelle: 0 Eintraege");
    PRUEFE(protokoll_ansicht_aufbereiten(quelle, 3, ziel, zustaende, 0) == 0,
           "kein Platz: 0 Eintraege");
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
    test_zustand_einstufung();
    test_ansicht_aufbereiten();
    test_ansicht_grenzen();

    protokoll_leeren();

    printf("\n%d Pruefungen, %d Fehler\n", geprueft, fehler);
    return fehler == 0 ? 0 : 1;
}
