/*
 * SpielBar v3 - creazione degli item e predicato di filtro
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#ifndef SPIELBAR_ITEMFACTORY_H
#define SPIELBAR_ITEMFACTORY_H

#include <Entry.h>

#include "SpielBarTypes.h"
#include "ListItems.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


// -------------------------------------------------------------
// Creazione item e predicato di filtro
// -------------------------------------------------------------

// Crea un item per un ref: risolve il symlink, classifica, calcola nome
// visualizzato, icona, origine pacchetto. Non tocca nessuna cache.

UnifiedListItem*
CreateItemForRef(const entry_ref& origRef, bool prefletHint, bool detectSymlink,
	const char* symlinkPath);

// Predicato di filtro, senza dipendenze da BView (piano par. 3.1).
// inCatalog: file e cartelle compaiono solo se preferiti o recenti.

bool
Matches(const UnifiedListItem* item, const FacetState& state,
	const ViewOptions& options, bool inCatalog);

#endif	// SPIELBAR_ITEMFACTORY_H
