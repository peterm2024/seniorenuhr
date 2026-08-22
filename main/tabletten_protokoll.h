/*
 * tabletten_protokoll.h — Langzeit-Aufzeichnung der Tabletten-Einnahmen.
 *
 * Bis hierher speicherte das Geraet nur den JEWEILIGEN Tag (kalender_speicher.c,
 * "tablette.txt") - allein als Schutz gegen einen Neustart mitten am Tag. Beim
 * Tageswechsel wurde dieser Stand verworfen; ein Rueckblick auf gestern oder
 * gar den Monat war damit unmoeglich (Peters Wunsch, 18.08.2026).
 *
 * Dieses Modul haengt jeden ABGESCHLOSSENEN Tag an eine eigene Datei an und
 * beantwortet daraus die Frage "wurde zuverlaessig genommen?".
 *
 * Bewusste Entscheidungen:
 *
 *  - Das Einnahme-Fenster (`ende_minute`) wird MITGESCHRIEBEN statt spaeter neu
 *    berechnet. Aendert Peter den Kalendereintrag im Nachhinein (andere Uhrzeit,
 *    geloescht), bleibt die Vergangenheit dadurch so bewertet, wie sie damals
 *    tatsaechlich galt.
 *  - Aufgezeichnet wird nur, was das Geraet auch wirklich beurteilen konnte:
 *    ein Tag landet erst im Protokoll, wenn das Geraet ueber den Tageswechsel
 *    hinweg gelaufen ist (siehe kalender_anzeige.c). War es aus, gab es auch
 *    keine Erinnerung - ein "vergessen" waere dann eine Falschaussage ueber
 *    Menschen, nicht ueber das Geraet.
 *  - Es gibt KEINE Rueckrechnung fuer die Zeit vor dem Einbau dieses Moduls -
 *    jene Daten existieren schlicht nicht.
 */
#ifndef TABLETTEN_PROTOKOLL_H
#define TABLETTEN_PROTOKOLL_H

#include <stdbool.h>
#include "esp_err.h"
#include "ics_parser.h"

/* Ein aufgezeichneter Einnahme-Vorgang (eine Tablette an einem Tag). */
typedef struct {
    int  tag_schluessel;          /* JJJJMMTT */
    int  soll_minute;             /* geplante Einnahme, Minuten seit Mitternacht; -1 = ganztaegig */
    int  ende_minute;             /* Ende des erlaubten Einnahme-Fensters (siehe Kopf) */
    int  ist_minute;              /* tatsaechliche Bestaetigung; -1 = gar nicht genommen */
    char titel[ICS_TITEL_MAX];
} tabletten_protokoll_eintrag_t;

/* So viele Auffaelligkeiten (vergessen bzw. deutlich zu spaet) nimmt eine
 * Bilanz namentlich auf. Alles darueber wird nur noch gezaehlt - die Liste
 * soll auf einen Bildschirm passen, nicht vollstaendig sein. */
#define TABLETTEN_PROTOKOLL_AUFFAELLIG_MAX 12

typedef struct {
    int  tage;                    /* Anzahl unterschiedlicher Tage im Zeitraum */
    int  gesamt;                  /* faellige Tabletten insgesamt */
    int  genommen;                /* davon im Fenster bestaetigt */
    int  zu_spaet;                /* davon nach dem Fenster bestaetigt */
    int  vergessen;               /* davon gar nicht bestaetigt */
    tabletten_protokoll_eintrag_t auffaellig[TABLETTEN_PROTOKOLL_AUFFAELLIG_MAX];
    int  auffaellig_anzahl;       /* tatsaechlich belegte Eintraege in `auffaellig` */
    int  auffaellig_gesamt;       /* wie viele es insgesamt gab (>= auffaellig_anzahl) */
} tabletten_protokoll_bilanz_t;

/* Bewertung eines aufgezeichneten Eintrags.
 *
 * Eigene Funktion, weil dieselbe Regel an zwei Stellen gebraucht wird: in der
 * Bilanz des Rueckblicks und in der Tagesansicht fuer einen vergangenen Tag.
 * Zwei Kopien duerften nie auseinanderlaufen - sonst zaehlte der Rueckblick
 * eine Tablette als "zu spaet", die das Tagesfenster gruen zeigt. */
typedef enum {
    TABLETTEN_ZUSTAND_GENOMMEN = 0, /* im erlaubten Fenster bestaetigt */
    TABLETTEN_ZUSTAND_ZU_SPAET,     /* erst nach dem Fenster bestaetigt */
    TABLETTEN_ZUSTAND_VERGESSEN,    /* gar nicht bestaetigt */
} tabletten_zustand_t;

tabletten_zustand_t tabletten_protokoll_zustand(const tabletten_protokoll_eintrag_t *eintrag);

/* Haengt einen abgeschlossenen Tag an. Mehrfaches Ablegen desselben Tages
 * wird verworfen (idempotent) - sonst zaehlte ein Neustart kurz nach
 * Mitternacht denselben Tag doppelt in die Bilanz. */
esp_err_t tabletten_protokoll_tag_ablegen(const tabletten_protokoll_eintrag_t *eintraege, int anzahl);

/* Wertet alle Eintraege ab `ab_tag_schluessel` (einschliesslich) aus. Liest die
 * Datei zeilenweise und zaehlt direkt mit, statt sie in den RAM zu laden - die
 * Bilanz ist dadurch unabhaengig von der Laenge der Aufzeichnung.
 * Liefert ESP_OK auch dann, wenn es noch gar keine Aufzeichnung gibt
 * (`ziel` ist dann vollstaendig genullt). */
esp_err_t tabletten_protokoll_bilanz_ermitteln(int ab_tag_schluessel, tabletten_protokoll_bilanz_t *ziel);

/* Alle Eintraege genau eines Tages, fuer den Rueckblick hinter den
 * Wochentag-Knoepfen. Rueckgabe: Anzahl geschriebener Eintraege. */
int tabletten_protokoll_tag_lesen(int tag_schluessel, tabletten_protokoll_eintrag_t *ziel, int max);

/* True, sobald fuer diesen Tag ueberhaupt eine Aufzeichnung existiert - so
 * laesst sich "an dem Tag war nichts faellig" von "darueber weiss das Geraet
 * nichts" unterscheiden (z.B. weil es aus war oder es das Protokoll damals
 * noch nicht gab). */
bool tabletten_protokoll_kennt_tag(int tag_schluessel);

#endif
