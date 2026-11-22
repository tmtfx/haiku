/*
 * SpielBar v3 - ordinamento dei nomi con il collatore della locale
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#ifndef SPIELBAR_COLLATION_H
#define SPIELBAR_COLLATION_H

#include <String.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


void
InitCollator();

int
CompareNames(const BString& a, const BString& b);

#endif	// SPIELBAR_COLLATION_H
