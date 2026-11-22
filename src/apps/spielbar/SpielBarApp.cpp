/*
 * SpielBar v3 - BApplication e main()
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#include <String.h>
#include <SupportDefs.h>
#include <stdio.h>

#include "SpielBarApp.h"
#include "SpielBarTypes.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


bool
ParseStartArgs(int32 argc, char** argv, FacetState* out)
{
	FacetState st;
	bool any = false;
	bool unknown = false;

	for (int32 i = 1; i < argc; i++) {
		BString arg(argv[i]);
		if (arg.ICompare("--apps") == 0) {
			st.kindMask |= kKindApp;
		} else if (arg.ICompare("--preflets") == 0) {
			st.kindMask |= kKindPreflet;
		} else if (arg.ICompare("--files") == 0) {
			st.kindMask |= kKindFile;
		} else if (arg.ICompare("--folders") == 0) {
			st.kindMask |= kKindFolder;
		} else if (arg.ICompare("--recents") == 0) {
			st.tags |= kTagRecent;
		} else if (arg.ICompare("--favorites") == 0) {
			st.tags |= kTagFavorite;
		} else if (arg.ICompare("--find") == 0) {
			st.world = kWorldSearch;
		} else if (arg.ICompare("--go") == 0) {
			st.world = kWorldGo;
		} else if (arg.ICompare("--disks") == 0) {
			st.world = kWorldDisks;
		} else {
			fprintf(stderr, "SpielBar: unknown argument '%s'\n", argv[i]);
			unknown = true;
			continue;
		}
		any = true;
	}

	if (unknown) {
		fprintf(stderr, "usage: SpielBar [--apps] [--preflets] [--files] [--folders]"
			" [--recents] [--favorites] [--find | --go | --disks]\n");
	}
	if (any)
		*out = st;
	return any;
}

int
main()
{
	DeskbarApp app;
	app.Run();
	return B_OK;
}
