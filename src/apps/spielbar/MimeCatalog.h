/*
 * SpielBar v3 - percorsi noti, classificazione MIME, origine dei pacchetti
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#ifndef SPIELBAR_MIMECATALOG_H
#define SPIELBAR_MIMECATALOG_H

#include <Entry.h>
#include <FindDirectory.h>
#include <Path.h>
#include <String.h>
#include <SupportDefs.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


// ---- Percorsi noti, classificazione, origine pacchetto -------

struct KnownPaths {
	BString sysPrefs, userPrefs, system, sysNonPackaged, userNonPackaged;

	KnownPaths()
	{
		_Get(B_SYSTEM_PREFERENCES_DIRECTORY, sysPrefs);
		_Get(B_USER_PREFERENCES_DIRECTORY, userPrefs);
		_Get(B_SYSTEM_DIRECTORY, system);
		_Get(B_SYSTEM_NONPACKAGED_DIRECTORY, sysNonPackaged);
		_Get(B_USER_NONPACKAGED_DIRECTORY, userNonPackaged);
	}

private:
	static void _Get(directory_which which, BString& out)
	{
		BPath p;
		if (find_directory(which, &p) == B_OK)
			out = p.Path();
	}
};

const KnownPaths&
Paths();

bool
PathStartsWith(const char* path, const BString& prefix);

bool
IsUnderPreferences(const entry_ref& ref);

// Una sola funzione di classificazione, usata da scansione, recenti,
// preferiti e risultati di Cerca.

uint32
ClassifyEntry(const entry_ref& ref, bool prefletHint, BString* signatureOut);

enum PackageOrigin { kNonPackaged, kPackaged, kSystem };

// SYS:PACKAGE contiene nome e versione del pacchetto, ad esempio
// "webpositive-r1~beta6_hrev60205-1". I pacchetti costruiti dall'albero di
// Haiku hanno "_hrev" nella versione (webpositive, haiku, haiku_devel...);
// gli altri pacchetti di base iniziano per "haiku".

bool
IsHaikuBasePackage(const BString& name);

// Trova il file .hpkg da cui viene un file installato (attributo
// SYS:PACKAGE_FILE) nelle cartelle dei pacchetti di sistema e utente.

bool
FindOriginPackage(const entry_ref& ref, entry_ref* out);

PackageOrigin
ReadPackageOrigin(const entry_ref& ref);

// Nome visualizzato: nome localizzato del file, altrimenti descrizione
// breve del MIME (solo app), altrimenti nome del file.

BString
DisplayNameFor(const char* signature, const entry_ref& ref);

#endif	// SPIELBAR_MIMECATALOG_H
