/*
 * SpielBar v3 - query asincrona per il mondo Cerca
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#ifndef SPIELBAR_SEARCHQUERY_H
#define SPIELBAR_SEARCHQUERY_H

#include <Messenger.h>
#include <String.h>
#include <SupportDefs.h>
#include <atomic>
#include <memory>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


// Pattern glob case-insensitive per BQuery. I caratteri speciali del glob
// (* ? [ \) vengono preceduti da backslash. Nota: il trucco "[aA]" copre
// solo l'ASCII; le lettere accentate sono confrontate cosi' come sono.

BString
BuildCaseInsensitivePattern(const BString& text, bool contains);

// -------------------------------------------------------------
// Worker di Cerca (non tiene puntatori alla finestra, piano par. 6)
// -------------------------------------------------------------

struct SearchParams {
	BMessenger target;
	std::shared_ptr<std::atomic<uint32> > generation;
	uint32 myGeneration;
	BString text;
};

int32
SearchWorker(void* data);

#endif	// SPIELBAR_SEARCHQUERY_H
