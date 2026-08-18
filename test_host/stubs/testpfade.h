/*
 * testpfade.h — biegt die Dateipfade des Tabletten-Protokolls auf temporaere
 * Namen um, damit der Host-Test nichts Echtes anfasst.
 *
 * Wird per "-include" eingebunden, NICHT per -D auf der Kommandozeile: ein
 * String-Makro mit Anfuehrungszeichen ueberlebt die PowerShell-Kommandozeile
 * nicht zuverlaessig (die inneren Quotes gehen verloren, das Makro ist dann
 * kein gueltiges String-Literal mehr und der Compiler bricht ab). Als Header
 * verhaelt es sich unter PowerShell, Bash und in der CI identisch.
 */
#ifndef TESTPFADE_H
#define TESTPFADE_H

#define PROTOKOLL_PFAD      "test_prot.txt"
#define PROTOKOLL_TEMP_PFAD "test_prot.tmp"

#endif
