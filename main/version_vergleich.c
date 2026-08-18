#include "version_vergleich.h"

#include <stdio.h>
#include <string.h>

/* Die vier Ordnungskriterien einer Version, absteigend nach Gewicht. */
typedef struct {
    int major;
    int minor;
    int patch;
    int commits; /* seit dem Tag; 0 = genau auf dem Release */
} version_t;

/* Zerlegt "v0.9.3-9-g393edfb-dirty" in seine Bestandteile.
 * Rueckgabe false, wenn nicht einmal "Major.Minor" lesbar ist - dann ist die
 * Angabe fuer eine Reihenfolge unbrauchbar (z. B. die "1", auf die ESP-IDF
 * zurueckfaellt, wenn "git describe" im Build fehlschlaegt). */
static bool zerlegen(const char *text, version_t *ziel)
{
    if (!text || !*text)
        return false;
    if (*text == 'v' || *text == 'V')
        text++;

    *ziel = (version_t){0};
    if (sscanf(text, "%d.%d.%d", &ziel->major, &ziel->minor, &ziel->patch) < 2)
        return false;

    /* Alles ab dem ersten Bindestrich ist das git-describe-Anhaengsel. Steht
     * dort eine Zahl, ist es der Commit-Abstand; steht dort etwas anderes
     * (z. B. direkt "-dirty"), bleibt es bei 0 - ein "dirty"-Build genau auf
     * dem Tag ist keine spaetere Version, sondern dieselbe mit lokalen
     * Aenderungen. */
    const char *anhang = strchr(text, '-');
    if (anhang)
        sscanf(anhang + 1, "%d", &ziel->commits);

    return true;
}

bool version_ist_neuer(const char *kandidat, const char *referenz)
{
    version_t k, r;
    if (!zerlegen(kandidat, &k) || !zerlegen(referenz, &r)) {
        /* Nicht deutbar - siehe Begruendung im Header: im Zweifel anbieten,
         * aber nur wenn sich die Angaben ueberhaupt unterscheiden. */
        if (!kandidat || !referenz)
            return false;
        return strcmp(kandidat, referenz) != 0;
    }

    if (k.major != r.major)
        return k.major > r.major;
    if (k.minor != r.minor)
        return k.minor > r.minor;
    if (k.patch != r.patch)
        return k.patch > r.patch;
    return k.commits > r.commits;
}
