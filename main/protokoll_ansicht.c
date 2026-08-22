#include "protokoll_ansicht.h"

#include <stdio.h>
#include <string.h>

int protokoll_ansicht_aufbereiten(const tabletten_protokoll_eintrag_t *quelle, int anzahl,
                                  kalender_tag_eintrag_t *eintraege,
                                  tabletten_zustand_t *zustaende, int max)
{
    if (!quelle || !eintraege || !zustaende || anzahl <= 0 || max <= 0)
        return 0;

    int belegt = 0;
    for (int i = 0; i < anzahl && belegt < max; i++) {
        const tabletten_protokoll_eintrag_t *q = &quelle[i];
        kalender_tag_eintrag_t *z = &eintraege[belegt];

        memset(z, 0, sizeof *z);
        /* Explizite Praezision statt nacktem "%s": ueber einen Zeiger
         * zugegriffene Array-Felder verlieren bei GCCs
         * Format-Truncation-Pruefung ihre bekannte Groesse (siehe die
         * gleichlautende Stelle in kalender_anzeige.c). */
        snprintf(z->titel, sizeof z->titel, "%.*s", ICS_TITEL_MAX - 1, q->titel);

        /* Negativ heisst laut tabletten_protokoll.h "ganztaegig"; eine
         * Uhrzeit des Vortags kann hier nicht stehen, weil das Protokoll
         * bewusst die Uhrzeit des JEWEILIGEN Tages speichert und nicht
         * relativ zu heute (siehe vortag_archivieren_falls_faellig in
         * kalender_anzeige.c). */
        if (q->soll_minute < 0) {
            z->ganztags = true;
        } else {
            z->stunde = q->soll_minute / 60;
            z->minute = q->soll_minute % 60;
        }

        z->ist_tablette = true;
        z->bestaetigt = q->ist_minute >= 0;
        z->bestaetigt_minute = q->ist_minute;

        zustaende[belegt] = tabletten_protokoll_zustand(q);
        belegt++;
    }
    return belegt;
}
