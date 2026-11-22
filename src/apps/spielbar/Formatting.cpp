/*
 * SpielBar v3 - stringhe per l'interfaccia: conteggi, descrizioni, tooltip
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#include <Catalog.h>
#include <String.h>
#include <StringFormat.h>
#include <SupportDefs.h>

#include "Formatting.h"
#include "SpielBarTypes.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


BString
ToolTipWith(const char* label, const char* shortcut)
{
	BString tip(label);
	if (shortcut != NULL && shortcut[0] != '\0')
		tip << "  (" << shortcut << ")";
	return tip;
}

BString
DescribeView(const ViewSpec& s)
{
	switch (s.world) {
		case kWorldDisks:
			return B_TRANSLATE("Disks");
		case kWorldGo:
			return B_TRANSLATE("Go");
		case kWorldSearch:
			return B_TRANSLATE("Search");
		default:
			break;
	}

	BString out;
	if (s.kindMask == 0) {
		out = B_TRANSLATE("All");
	} else {
		bool first = true;
		struct { uint32 bit; BString name; } kinds[4] = {
			{ kKindApp, B_TRANSLATE("Apps") },
			{ kKindPreflet, B_TRANSLATE("Preflets") },
			{ kKindFile, B_TRANSLATE("Files") },
			{ kKindFolder, B_TRANSLATE("Folders") }
		};
		for (int i = 0; i < 4; i++) {
			if (s.kindMask & kinds[i].bit) {
				if (!first)
					out << " + ";
				out << kinds[i].name;
				first = false;
			}
		}
	}
	if (s.tags != 0) {
		out << " \xC2\xB7 ";
		bool first = true;
		if (s.tags & kTagRecent) {
			out << B_TRANSLATE_COMMENT("Recent", "Filter chip and section header");
			first = false;
		}
		if (s.tags & kTagFavorite) {
			if (!first)
				out << " + ";
			out << B_TRANSLATE("Favorites");
		}
	}
	return out;
}

BString
FormatCount(int32 n, int mode)	// 0 elementi, 1 risultati, 2 risultati troncati
{
	const char* pattern;
	if (mode == 0)
		pattern = B_TRANSLATE("{0, plural, one{# item} other{# items}}");
	else if (mode == 1)
		pattern = B_TRANSLATE("{0, plural, one{# result} other{# results}}");
	else
		pattern = B_TRANSLATE("{0, plural, other{#+ results}}");

	BString out;
	BStringFormat format(pattern);
	if (format.Format(out, (int64)n) != B_OK)
		out.SetToFormat("%d", (int)n);
	return out;
}
