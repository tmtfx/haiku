/*
 * SpielBar v3 - ordinamento dei nomi con il collatore della locale
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#include <Collator.h>
#include <Locale.h>
#include <String.h>

#include "Collation.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


// ---- Collator (piano par. 3.7) ------------------------------

static BCollator* gCollator = NULL;

void
InitCollator()
{
	if (gCollator != NULL)
		return;
	BCollator collator;
	if (BLocale::Default()->GetCollator(&collator) == B_OK)
		gCollator = new BCollator(collator);
}

int
CompareNames(const BString& a, const BString& b)
{
	if (gCollator != NULL)
		return gCollator->Compare(a.String(), b.String());
	return strcasecmp(a.String(), b.String());
}
