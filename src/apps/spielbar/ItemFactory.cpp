/*
 * SpielBar v3 - creazione degli item e predicato di filtro
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#include <Bitmap.h>
#include <Entry.h>
#include <Node.h>
#include <Path.h>
#include <String.h>
#include <SupportDefs.h>

#include "ItemFactory.h"
#include "SpielBarTypes.h"
#include "UiMetrics.h"
#include "MimeCatalog.h"
#include "IconSupport.h"
#include "ListItems.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


UnifiedListItem*
CreateItemForRef(const entry_ref& origRef, bool prefletHint, bool detectSymlink,
	const char* symlinkPath)
{
	BEntry target(&origRef, true);
	if (target.InitCheck() != B_OK || !target.Exists())
		return NULL;

	entry_ref targetRef;
	node_ref node;
	if (target.GetRef(&targetRef) != B_OK || target.GetNodeRef(&node) != B_OK)
		return NULL;

	BPath fullPath;
	if (target.GetPath(&fullPath) != B_OK)
		return NULL;
	BPath parentPath;
	fullPath.GetParent(&parentPath);

	BString signature;
	bool hint = prefletHint || IsUnderPreferences(origRef);
	uint32 kind = ClassifyEntry(targetRef, hint, &signature);

	BString name = DisplayNameFor(signature.String(), targetRef);
	BBitmap* icon = LoadBestIconForRef(targetRef,
		signature.IsEmpty() ? NULL : signature.String(), UI().rowIcon);

	UnifiedListItem* item = new UnifiedListItem(name.String(),
		parentPath.Path(), targetRef, signature.String(), icon, symlinkPath);
	item->SetKind(kind);
	item->SetNode(node);
	item->SetFileName(targetRef.name);

	uint32 flags = 0;
	if (detectSymlink) {
		BEntry raw(&origRef, false);
		if (raw.InitCheck() == B_OK && raw.IsSymLink())
			flags |= kFlagSymlink;
	}
	if (kind == kKindApp || kind == kKindPreflet) {
		switch (ReadPackageOrigin(targetRef)) {
			case kSystem:
				flags |= kFlagSystem;
				break;
			case kPackaged:
				flags |= kFlagPackaged;
				break;
			default:
				break;
		}
	}
	item->SetItemFlags(flags);
	return item;
}

bool
Matches(const UnifiedListItem* item, const FacetState& state,
	const ViewOptions& options, bool inCatalog)
{
	uint32 kind = item->Kind();
	uint32 flags = item->ItemFlags();

	if (inCatalog && (kind & (kKindFile | kKindFolder))
		&& !(flags & (kFlagFavorite | kFlagRecent)))
		return false;
	if (state.kindMask != 0 && !(state.kindMask & kind))
		return false;
	if (state.tags != 0) {
		// Recent e Preferiti in OR.
		bool ok = ((state.tags & kTagRecent) && (flags & kFlagRecent))
			|| ((state.tags & kTagFavorite) && (flags & kFlagFavorite));
		if (!ok)
			return false;
	}
	if (options.hideSystem && (flags & kFlagSystem)
		&& (kind & (kKindApp | kKindPreflet)))
		return false;
	if (!state.text.IsEmpty()) {
		if (item->Name().IFindFirst(state.text) < 0
			&& item->FileName().IFindFirst(state.text) < 0)
			return false;
	}
	return true;
}
