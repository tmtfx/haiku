/*
 * SpielBar v3 - apertura di cartelle e "Get info" tramite Tracker
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#ifndef SPIELBAR_TRACKERBRIDGE_H
#define SPIELBAR_TRACKERBRIDGE_H

#include <Entry.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


// -------------------------------------------------------------
// Helpers
// -------------------------------------------------------------

void
OpenInfo(const char* path);

void
RevealInTracker(const entry_ref& ref);

void
OpenFolderInTracker(const char* path);

#endif	// SPIELBAR_TRACKERBRIDGE_H
