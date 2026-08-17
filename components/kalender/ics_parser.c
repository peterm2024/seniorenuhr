/*
 * ics_parser.c — Implementierung, siehe ics_parser.h.
 *
 * Bekannte, bewusste Vereinfachungen:
 *   - TZID-Zeiten werden als lokale Zeit (Europe/Berlin) gelesen; die Uhr
 *     läuft ohnehin in dieser Zone.
 *   - Mehrtägige Termine MIT Uhrzeit zählen nur an ihrem Starttag.
 *   - BYDAY mit Positionsangabe bei MONTHLY ("2. Dienstag") wird nicht
 *     unterstützt — Google nutzt für Tablettenpläne DAILY/WEEKLY.
 */
#include "ics_parser.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Kalender-Arithmetik (Algorithmen von Howard Hinnant, gemeinfrei)    */
/* ------------------------------------------------------------------ */

/* Tage seit 1970-01-01; funktioniert auch weit in Vergangenheit/Zukunft. */
static long tage_seit_epoche(int j, int m, int t)
{
    j -= m <= 2;
    long era = (j >= 0 ? j : j - 399) / 400;
    unsigned joe = (unsigned)(j - era * 400);
    unsigned tdj = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + t - 1);
    unsigned tde = joe * 365 + joe / 4 - joe / 100 + tdj;
    return era * 146097 + (long)tde - 719468;
}

static void datum_aus_tagen(long z, int *j, int *m, int *t)
{
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned tde = (unsigned)(z - era * 146097);
    unsigned joe = (tde - tde / 1460 + tde / 36524 - tde / 146096) / 365;
    long jahr = (long)joe + era * 400;
    unsigned tdj = tde - (365 * joe + joe / 4 - joe / 100);
    unsigned mp = (5 * tdj + 2) / 153;
    *t = (int)(tdj - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *j = (int)(jahr + (*m <= 2));
}

/* Wochentag, Montag = 0 ... Sonntag = 6 */
static int wochentag(long tage)
{
    long w = (tage + 3) % 7;
    return (int)(w < 0 ? w + 7 : w);
}

/* Letzter Sonntag eines Monats als Tage-seit-Epoche. */
static long letzter_sonntag(int jahr, int monat)
{
    static const int mlen[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int tage_im_monat = mlen[monat - 1];
    if (monat == 2 && ((jahr % 4 == 0 && jahr % 100 != 0) || jahr % 400 == 0))
        tage_im_monat = 29;
    long letzter = tage_seit_epoche(jahr, monat, tage_im_monat);
    return letzter - ((wochentag(letzter) + 1) % 7);
}

/* Rechnet einen UTC-Zeitpunkt nach Europe/Berlin um (MEZ/MESZ).
 * Sommerzeit: letzter Sonntag im März 01:00 UTC bis letzter Sonntag
 * im Oktober 01:00 UTC. */
static void utc_nach_berlin(ics_zeit_t *z)
{
    long tage = tage_seit_epoche(z->jahr, z->monat, z->tag);
    long min_utc = tage * 1440L + z->stunde * 60L + z->minute;

    long dst_start = letzter_sonntag(z->jahr, 3)  * 1440L + 60;
    long dst_ende  = letzter_sonntag(z->jahr, 10) * 1440L + 60;
    int offset = (min_utc >= dst_start && min_utc < dst_ende) ? 120 : 60;

    long min_lokal = min_utc + offset;
    long t = min_lokal / 1440L;
    long rest = min_lokal - t * 1440L;
    datum_aus_tagen(t, &z->jahr, &z->monat, &z->tag);
    z->stunde = (int)(rest / 60);
    z->minute = (int)(rest % 60);
}

/* ------------------------------------------------------------------ */
/* Kleine String-Helfer                                                */
/* ------------------------------------------------------------------ */

static bool beginnt_mit_ci(const char *s, const char *praefix)
{
    while (*praefix) {
        char a = *s++, b = *praefix++;
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) return false;
    }
    return true;
}

static int zahl(const char *s, int n)
{
    int wert = 0;
    for (int i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        wert = wert * 10 + (s[i] - '0');
    }
    return wert;
}

/* ------------------------------------------------------------------ */
/* Zeitwerte parsen                                                    */
/* ------------------------------------------------------------------ */

/* "20260715T150000", "20260715T130000Z" oder "20260715" (nur Datum).
 * Liefert false bei Murks. */
static bool parse_zeitwert(const char *wert, ics_zeit_t *z, bool *ganztags)
{
    memset(z, 0, sizeof *z);
    z->jahr  = zahl(wert, 4);
    z->monat = zahl(wert + 4, 2);
    z->tag   = zahl(wert + 6, 2);
    if (z->jahr < 0 || z->monat < 1 || z->monat > 12 || z->tag < 1 || z->tag > 31)
        return false;

    if (wert[8] != 'T') {          /* reines Datum -> ganztägig */
        *ganztags = true;
        return true;
    }
    *ganztags = false;
    z->stunde = zahl(wert + 9, 2);
    z->minute = zahl(wert + 11, 2);
    if (z->stunde < 0 || z->stunde > 23 || z->minute < 0 || z->minute > 59)
        return false;
    /* Sekunden ignorieren; 'Z' am Ende = UTC */
    if (strchr(wert + 13, 'Z') != NULL)
        utc_nach_berlin(z);
    return true;
}

static long zeit_in_minuten(const ics_zeit_t *z, bool ganztags)
{
    long tage = tage_seit_epoche(z->jahr, z->monat, z->tag);
    if (ganztags)
        return tage * 1440L + 1439; /* zählt bis Tagesende */
    return tage * 1440L + z->stunde * 60L + z->minute;
}

/* ------------------------------------------------------------------ */
/* RRULE                                                               */
/* ------------------------------------------------------------------ */

enum freq { FREQ_KEINE, FREQ_TAEGLICH, FREQ_WOECHENTLICH, FREQ_MONATLICH, FREQ_JAEHRLICH };

typedef struct {
    enum freq freq;
    int  interval;          /* >= 1 */
    int  count;             /* 0 = unbegrenzt */
    bool hat_until;
    long until_minuten;     /* lokale Zeit, inklusiv */
    unsigned byday;         /* Bitmaske, Bit 0 = Montag */
    int  bymonthday;        /* 0 = nicht gesetzt */
} rrule_t;

static int byday_index(const char *code)
{
    static const char *namen[7] = {"MO","TU","WE","TH","FR","SA","SU"};
    for (int i = 0; i < 7; i++)
        if (code[0] == namen[i][0] && code[1] == namen[i][1]) return i;
    return -1;
}

static void parse_rrule(const char *text, rrule_t *rr)
{
    memset(rr, 0, sizeof *rr);
    rr->interval = 1;

    const char *p = text;
    while (*p) {
        const char *ende = strchr(p, ';');
        size_t len = ende ? (size_t)(ende - p) : strlen(p);

        if (beginnt_mit_ci(p, "FREQ=")) {
            const char *w = p + 5;
            if      (beginnt_mit_ci(w, "DAILY"))   rr->freq = FREQ_TAEGLICH;
            else if (beginnt_mit_ci(w, "WEEKLY"))  rr->freq = FREQ_WOECHENTLICH;
            else if (beginnt_mit_ci(w, "MONTHLY")) rr->freq = FREQ_MONATLICH;
            else if (beginnt_mit_ci(w, "YEARLY"))  rr->freq = FREQ_JAEHRLICH;
        } else if (beginnt_mit_ci(p, "INTERVAL=")) {
            int v = atoi(p + 9);
            if (v >= 1) rr->interval = v;
        } else if (beginnt_mit_ci(p, "COUNT=")) {
            int v = atoi(p + 6);
            if (v >= 1) rr->count = v;
        } else if (beginnt_mit_ci(p, "UNTIL=")) {
            ics_zeit_t z; bool gz;
            char puffer[24] = {0};
            size_t n = len - 6 < 23 ? len - 6 : 23;
            memcpy(puffer, p + 6, n);
            if (parse_zeitwert(puffer, &z, &gz)) {
                rr->hat_until = true;
                rr->until_minuten = zeit_in_minuten(&z, gz);
            }
        } else if (beginnt_mit_ci(p, "BYDAY=")) {
            const char *w = p + 6, *feld_ende = p + len;
            while (w < feld_ende) {
                /* evtl. Positionszahl ("-1SU") überspringen */
                while (w < feld_ende && (*w == '-' || *w == '+' || (*w >= '0' && *w <= '9')))
                    w++;
                if (w + 1 < feld_ende) {
                    int idx = byday_index(w);
                    if (idx >= 0) rr->byday |= 1u << idx;
                }
                while (w < feld_ende && *w != ',') w++;
                if (w < feld_ende) w++;
            }
        } else if (beginnt_mit_ci(p, "BYMONTHDAY=")) {
            int v = atoi(p + 11);
            if (v >= 1 && v <= 31) rr->bymonthday = v;
        }

        if (!ende) break;
        p = ende + 1;
    }
}

/* ------------------------------------------------------------------ */
/* Ein VEVENT                                                          */
/* ------------------------------------------------------------------ */

#define MAX_EXDATE 16

typedef struct {
    char       titel[256];
    char       beschreibung[256];
    ics_zeit_t start;
    bool       hat_start;
    bool       ganztags;
    ics_zeit_t ende;
    bool       hat_ende;
    bool       ende_ganztags;
    char       rrule_text[256];
    bool       hat_rrule;
    ics_zeit_t exdate[MAX_EXDATE];
    bool       exdate_ganztags[MAX_EXDATE];
    int        n_exdate;
    bool       abgesagt;
} vevent_t;

static bool ist_exdate(const vevent_t *ev, long ziel_tage)
{
    for (int i = 0; i < ev->n_exdate; i++) {
        const ics_zeit_t *x = &ev->exdate[i];
        if (tage_seit_epoche(x->jahr, x->monat, x->tag) != ziel_tage)
            continue;
        if (ev->exdate_ganztags[i] || ev->ganztags)
            return true;
        if (x->stunde == ev->start.stunde && x->minute == ev->start.minute)
            return true;
    }
    return false;
}

/* Prüft, ob ein wiederkehrender Termin am Zieltag stattfindet. */
static bool rrule_trifft_tag(const vevent_t *ev, const rrule_t *rr, long ziel)
{
    long start = tage_seit_epoche(ev->start.jahr, ev->start.monat, ev->start.tag);
    if (ziel < start)
        return false;

    long start_minuten = ev->start.stunde * 60L + ev->start.minute;

    if (rr->freq == FREQ_TAEGLICH || rr->freq == FREQ_WOECHENTLICH) {
        if (ziel - start > 100000)   /* Notbremse: > ~270 Jahre */
            return false;
        long montag_start = start - wochentag(start);
        int gezaehlt = 0;

        for (long t = start; t <= ziel; t++) {
            bool trifft;
            if (rr->freq == FREQ_TAEGLICH) {
                trifft = ((t - start) % rr->interval) == 0;
            } else {
                int wt = wochentag(t);
                bool tag_ok = rr->byday ? ((rr->byday >> wt) & 1u)
                                        : (wt == wochentag(start));
                trifft = tag_ok && (((t - montag_start) / 7) % rr->interval) == 0;
            }
            if (!trifft)
                continue;

            if (rr->hat_until && t * 1440L + start_minuten > rr->until_minuten)
                return false;
            gezaehlt++;
            if (rr->count && gezaehlt > rr->count)
                return false;
            if (t == ziel)
                return !ist_exdate(ev, ziel);
        }
        return false;
    }

    if (rr->freq == FREQ_MONATLICH || rr->freq == FREQ_JAEHRLICH) {
        int zj, zm, zt;
        datum_aus_tagen(ziel, &zj, &zm, &zt);
        int soll_tag = rr->bymonthday ? rr->bymonthday : ev->start.tag;
        if (zt != soll_tag)
            return false;
        if (rr->freq == FREQ_JAEHRLICH && zm != ev->start.monat)
            return false;

        long monate = (zj - ev->start.jahr) * 12L + (zm - ev->start.monat);
        long schritt = (rr->freq == FREQ_MONATLICH) ? rr->interval
                                                    : rr->interval * 12L;
        if (monate < 0 || monate % schritt != 0)
            return false;
        if (rr->hat_until && ziel * 1440L + start_minuten > rr->until_minuten)
            return false;
        /* Näherung: jeder Schritt ergibt ein gültiges Datum (Tag <= 28 o. ä.) */
        if (rr->count && monate / schritt + 1 > rr->count)
            return false;
        return !ist_exdate(ev, ziel);
    }

    return false;
}

static bool event_trifft_tag(const vevent_t *ev, long ziel)
{
    if (!ev->hat_start || ev->abgesagt)
        return false;

    if (ev->hat_rrule) {
        rrule_t rr;
        parse_rrule(ev->rrule_text, &rr);
        if (rr.freq == FREQ_KEINE)
            return false;
        return rrule_trifft_tag(ev, &rr, ziel);
    }

    long start = tage_seit_epoche(ev->start.jahr, ev->start.monat, ev->start.tag);
    if (ev->ganztags && ev->hat_ende && ev->ende_ganztags) {
        /* DTEND ist bei ganztägigen Terminen exklusiv */
        long ende = tage_seit_epoche(ev->ende.jahr, ev->ende.monat, ev->ende.tag);
        return ziel >= start && ziel < ende;
    }
    return ziel == start;
}

/* ------------------------------------------------------------------ */
/* Text aufbereiten: ICS-Escapes auflösen (\n -> Leerzeichen, \\ -> \,  */
/* \, -> , usw.) - gemeinsam fuer Titel (nach Praefix-Erkennung) und    */
/* Beschreibung genutzt.                                                */
/* ------------------------------------------------------------------ */

static void text_escape_kopieren(const char *roh, char *ziel, size_t ziel_kapazitaet)
{
    const char *p = roh;
    size_t o = 0;
    while (*p && o < ziel_kapazitaet - 1) {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
                case 'n': case 'N': ziel[o++] = ' '; break;
                default:            ziel[o++] = *p;  break;
            }
            p++;
        } else {
            ziel[o++] = *p++;
        }
    }
    ziel[o] = '\0';
}

/* Praefixe, die einen Kalendereintrag als Tablette kennzeichnen.
 *
 * BEWUSST SPRACHUNABHAENGIG: hier werden IMMER alle Varianten erkannt, egal
 * welche Oberflaechensprache eingestellt ist (siehe texte.h). Das Praefix
 * steht in den KALENDERDATEN des Nutzers, nicht in der Oberflaeche - wuerde
 * es der Spracheinstellung folgen, haette ein Umschalten schlagartig alle
 * bestehenden Eintraege zu gewoehnlichen Terminen gemacht und damit die
 * Tabletten-Erinnerung stillgelegt. Eine neue Sprache ergaenzt hier also nur
 * eine Zeile, nimmt aber nie eine weg. */
static const char *const TABLETTEN_PRAEFIXE[] = {
    "TABLETTE:", "TABLETTEN:",   /* deutsch */
    "PILL:", "PILLS:", "MED:",   /* englisch */
};

/* Titel aufbereiten: Tabletten-Praefix erkennen, dann Escapes aufloesen. */
static void titel_uebernehmen(const char *roh, ics_termin_t *ziel)
{
    const char *p = roh;

    ziel->ist_tablette = false;
    for (size_t i = 0; i < sizeof TABLETTEN_PRAEFIXE / sizeof TABLETTEN_PRAEFIXE[0]; i++) {
        if (beginnt_mit_ci(p, TABLETTEN_PRAEFIXE[i])) {
            ziel->ist_tablette = true;
            p = strchr(p, ':') + 1;
            while (*p == ' ') p++;
            break;
        }
    }

    text_escape_kopieren(p, ziel->titel, sizeof ziel->titel);
}

/* Beschreibung aufbereiten: nur Escapes aufloesen, kein Praefix-Konzept. */
static void beschreibung_uebernehmen(const char *roh, ics_termin_t *ziel)
{
    text_escape_kopieren(roh, ziel->beschreibung, sizeof ziel->beschreibung);
}

/* ------------------------------------------------------------------ */
/* Zeilenleser mit Entfaltung (RFC 5545: Folgezeile beginnt mit        */
/* Leerzeichen oder Tab)                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *pos;
    const char *ende;
} leser_t;

/* Liest die nächste logische (entfaltete) Zeile. false am Ende. */
static bool naechste_zeile(leser_t *l, char *puffer, size_t puffer_groesse)
{
    if (l->pos >= l->ende)
        return false;

    size_t o = 0;
    while (l->pos < l->ende) {
        char c = *l->pos++;
        if (c == '\r')
            continue;
        if (c == '\n') {
            /* gefaltete Fortsetzung? */
            if (l->pos < l->ende && (*l->pos == ' ' || *l->pos == '\t')) {
                l->pos++;
                continue;
            }
            break;
        }
        if (o < puffer_groesse - 1)
            puffer[o++] = c;
    }
    puffer[o] = '\0';
    return true;
}

/* Zerlegt "NAME;PARAM=X;PARAM=Y:WERT". Params-Zeiger darf NULL bleiben. */
static bool zerlege_zeile(char *zeile, const char **name,
                          const char **params, const char **wert)
{
    char *doppelpunkt = strchr(zeile, ':');
    if (!doppelpunkt)
        return false;
    *doppelpunkt = '\0';
    *wert = doppelpunkt + 1;

    char *semikolon = strchr(zeile, ';');
    if (semikolon) {
        *semikolon = '\0';
        *params = semikolon + 1;
    } else {
        *params = "";
    }
    *name = zeile;
    return true;
}

/* ------------------------------------------------------------------ */
/* Sortierung: ganztägig zuerst, dann nach Uhrzeit                     */
/* ------------------------------------------------------------------ */

static int vergleiche_termine(const void *a, const void *b)
{
    const ics_termin_t *ta = (const ics_termin_t *)a;
    const ics_termin_t *tb = (const ics_termin_t *)b;
    if (ta->ganztags != tb->ganztags)
        return ta->ganztags ? -1 : 1;
    int ma = ta->beginn.stunde * 60 + ta->beginn.minute;
    int mb = tb->beginn.stunde * 60 + tb->beginn.minute;
    if (ma != mb)
        return ma - mb;
    return strcmp(ta->titel, tb->titel);
}

/* ------------------------------------------------------------------ */
/* Hauptfunktion                                                       */
/* ------------------------------------------------------------------ */

int ics_termine_fuer_tag(const char *ics_text, size_t laenge,
                         int jahr, int monat, int tag,
                         ics_termin_t *ergebnis, int max_ergebnisse)
{
    if (!ics_text || !ergebnis || max_ergebnisse <= 0 ||
        monat < 1 || monat > 12 || tag < 1 || tag > 31)
        return -1;

    long ziel = tage_seit_epoche(jahr, monat, tag);
    leser_t leser = { ics_text, ics_text + laenge };
    char zeile[1024];

    vevent_t ev;
    bool im_event = false;
    int alarm_tiefe = 0;   /* Inhalte von VALARM & Co. ignorieren */
    int anzahl = 0;

    while (naechste_zeile(&leser, zeile, sizeof zeile)) {
        const char *name, *params, *wert;
        if (!zerlege_zeile(zeile, &name, &params, &wert))
            continue;

        if (strcmp(name, "BEGIN") == 0) {
            if (strcmp(wert, "VEVENT") == 0 && !im_event) {
                memset(&ev, 0, sizeof ev);
                im_event = true;
                alarm_tiefe = 0;
            } else if (im_event) {
                alarm_tiefe++;
            }
            continue;
        }
        if (strcmp(name, "END") == 0) {
            if (im_event && alarm_tiefe > 0) {
                alarm_tiefe--;
            } else if (im_event && strcmp(wert, "VEVENT") == 0) {
                im_event = false;
                if (anzahl < max_ergebnisse && event_trifft_tag(&ev, ziel)) {
                    ics_termin_t *t = &ergebnis[anzahl++];
                    memset(t, 0, sizeof *t);
                    titel_uebernehmen(ev.titel, t);
                    beschreibung_uebernehmen(ev.beschreibung, t);
                    t->ganztags = ev.ganztags;
                    t->beginn = ev.start;
                    t->beginn.jahr = jahr;
                    t->beginn.monat = monat;
                    t->beginn.tag = tag;
                    if (ev.ganztags) {
                        t->beginn.stunde = 0;
                        t->beginn.minute = 0;
                    }
                    /* Nur eine echte DTEND-Uhrzeit zaehlt als Einnahme-
                     * Fenster-Ende (kalender_tablette_status, main/
                     * kalender_anzeige.c) - ein ganztaegiges DTEND ist ein
                     * Datum, keine Uhrzeit, und waere hier bedeutungslos. */
                    t->hat_ende = ev.hat_ende && !ev.ende_ganztags;
                    if (t->hat_ende) {
                        t->ende = ev.ende;
                        t->ende.jahr = jahr;
                        t->ende.monat = monat;
                        t->ende.tag = tag;
                    }
                }
            }
            continue;
        }
        if (!im_event || alarm_tiefe > 0)
            continue;

        if (strcmp(name, "SUMMARY") == 0) {
            strncpy(ev.titel, wert, sizeof ev.titel - 1);
        } else if (strcmp(name, "DESCRIPTION") == 0) {
            strncpy(ev.beschreibung, wert, sizeof ev.beschreibung - 1);
        } else if (strcmp(name, "DTSTART") == 0) {
            ev.hat_start = parse_zeitwert(wert, &ev.start, &ev.ganztags);
        } else if (strcmp(name, "DTEND") == 0) {
            ev.hat_ende = parse_zeitwert(wert, &ev.ende, &ev.ende_ganztags);
        } else if (strcmp(name, "RRULE") == 0) {
            strncpy(ev.rrule_text, wert, sizeof ev.rrule_text - 1);
            ev.hat_rrule = true;
        } else if (strcmp(name, "EXDATE") == 0) {
            /* mehrere Werte durch Komma getrennt möglich */
            const char *p = wert;
            while (p && *p && ev.n_exdate < MAX_EXDATE) {
                char puffer[24] = {0};
                const char *komma = strchr(p, ',');
                size_t n = komma ? (size_t)(komma - p) : strlen(p);
                if (n > 23) n = 23;
                memcpy(puffer, p, n);
                ics_zeit_t z; bool gz;
                if (parse_zeitwert(puffer, &z, &gz)) {
                    ev.exdate[ev.n_exdate] = z;
                    ev.exdate_ganztags[ev.n_exdate] = gz;
                    ev.n_exdate++;
                }
                p = komma ? komma + 1 : NULL;
            }
        } else if (strcmp(name, "STATUS") == 0) {
            if (beginnt_mit_ci(wert, "CANCELLED"))
                ev.abgesagt = true;
        }
    }

    qsort(ergebnis, (size_t)anzahl, sizeof ergebnis[0], vergleiche_termine);
    return anzahl;
}
