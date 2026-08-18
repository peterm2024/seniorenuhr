/*
 * test_version.c — Tests für den Firmware-Versionsvergleich
 * (main/version_vergleich.c), laufen auf dem PC.
 *
 * Anlass ist Peters Beobachtung vom 18.08.2026: das Gerät zeigte einen
 * Update-Pfeil, obwohl die laufende Version (ein selbst geflashter
 * Entwicklungsstand) NEUER war als das im Netz liegende Release.
 *
 * Bauen & starten:  test_host\teste.ps1
 */
#include <stdio.h>

#include "version_vergleich.h"

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

static void test_der_gemeldete_fall(void)
{
    printf("\nDer gemeldete Fall: Entwicklungsstand gegen Release\n");

    /* Genau die Werte vom Geraet: laufend v0.9.3-9-g393edfb-dirty,
     * im Netz liegt v0.9.3. */
    PRUEFE(!version_ist_neuer("v0.9.3", "v0.9.3-9-g393edfb-dirty"),
           "aelteres Release gilt NICHT als Update fuer einen Entwicklungsstand");
    PRUEFE(version_ist_neuer("v0.9.3-9-g393edfb-dirty", "v0.9.3"),
           "der Entwicklungsstand ist umgekehrt neuer als das Release");
}

static void test_uebliche_reihenfolge(void)
{
    printf("\nUebliche Versionsfolge\n");

    PRUEFE(version_ist_neuer("v0.9.4", "v0.9.3"), "hoeherer Patch ist neuer");
    PRUEFE(!version_ist_neuer("v0.9.3", "v0.9.4"), "niedrigerer Patch ist nicht neuer");
    PRUEFE(version_ist_neuer("v0.10.0", "v0.9.9"), "Minor wird als Zahl verglichen, nicht als Text");
    PRUEFE(version_ist_neuer("v1.0.0", "v0.99.99"), "hoeherer Major schlaegt alles");
    PRUEFE(!version_ist_neuer("v0.9.3", "v0.9.3"), "gleiche Version ist kein Update");
}

static void test_commit_abstand(void)
{
    printf("\nCommit-Abstand als letztes Kriterium\n");

    PRUEFE(version_ist_neuer("v0.9.3-10-gabc", "v0.9.3-9-gdef"),
           "mehr Commits nach demselben Tag ist neuer");
    PRUEFE(!version_ist_neuer("v0.9.3-9-gdef", "v0.9.3-10-gabc"),
           "weniger Commits ist nicht neuer");
    PRUEFE(version_ist_neuer("v0.9.4", "v0.9.3-99-gabc"),
           "ein neues Release schlaegt jeden Entwicklungsstand des Vorgaengers");
    PRUEFE(!version_ist_neuer("v0.9.3-dirty", "v0.9.3"),
           "nur lokal geaendert (dirty) ist keine spaetere Version");
}

static void test_formatvarianten(void)
{
    printf("\nSchreibweisen\n");

    PRUEFE(version_ist_neuer("0.9.4", "0.9.3"), "funktioniert auch ohne fuehrendes v");
    PRUEFE(version_ist_neuer("v0.9.4", "0.9.3"), "gemischte Schreibweise ist unproblematisch");
    PRUEFE(version_ist_neuer("v1.0", "v0.9"), "zweistellige Angabe (ohne Patch) wird akzeptiert");
    PRUEFE(!version_ist_neuer("v1.0", "v1.0.0"), "1.0 und 1.0.0 sind dieselbe Version");
}

static void test_unbrauchbare_angaben(void)
{
    printf("\nNicht deutbare Angaben verschlucken kein echtes Update\n");

    /* "1" ist der Wert, auf den ESP-IDF zurueckfaellt, wenn "git describe"
     * im Build fehlschlaegt - dann ist keine Reihenfolge bestimmbar. */
    PRUEFE(version_ist_neuer("v0.9.3", "1"),
           "gegen eine unbrauchbare laufende Version wird im Zweifel angeboten");
    PRUEFE(!version_ist_neuer("1", "1"),
           "zwei identische unbrauchbare Angaben sind trotzdem kein Update");
    PRUEFE(!version_ist_neuer(NULL, "v0.9.3"), "NULL fuehrt nicht zu einem Update");
    PRUEFE(!version_ist_neuer("v0.9.3", NULL), "NULL als Referenz fuehrt nicht zu einem Update");
    PRUEFE(!version_ist_neuer("", ""), "leere Angaben fuehren nicht zu einem Update");
}

int main(void)
{
    printf("Versionsvergleich\n");

    test_der_gemeldete_fall();
    test_uebliche_reihenfolge();
    test_commit_abstand();
    test_formatvarianten();
    test_unbrauchbare_angaben();

    printf("\n%d Pruefungen, %d Fehler\n", geprueft, fehler);
    return fehler == 0 ? 0 : 1;
}
