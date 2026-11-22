/*
 * SpielBar v3 - la finestra principale
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#include <AppFileInfo.h>
#include <Application.h>
#include <Catalog.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <ListItem.h>
#include <Menu.h>
#include <Message.h>
#include <Node.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <Roster.h>
#include <StorageDefs.h>
#include <String.h>
#include <SupportDefs.h>
#include <algorithm>
#include <vector>

#include "DeskbarWindow.h"
#include "SpielBarTypes.h"
#include "Collation.h"
#include "ListItems.h"
#include "ItemFactory.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


// ---- Indici e cache ----------------------------------------------
UnifiedListItem*
DeskbarWindow::_CacheEnsure(const entry_ref& ref, bool prefletHint,
	bool detectSymlink)
{
	BEntry target(&ref, true);
	node_ref node;
	if (target.InitCheck() != B_OK || !target.Exists()
		|| target.GetNodeRef(&node) != B_OK)
		return NULL;

	ItemCache::iterator it = fCache.find(node);
	if (it != fCache.end())
		return it->second;

	UnifiedListItem* item = CreateItemForRef(ref, prefletHint,
		detectSymlink, NULL);
	if (item == NULL)
		return NULL;
	fCache[node] = item;
	return item;
}

void
DeskbarWindow::_ScanCatalog()
{
	BPath path;
	if (find_directory(B_SYSTEM_APPS_DIRECTORY, &path) == B_OK)
		_ScanDirectory(path.Path(), false, 0);
	if (find_directory(B_SYSTEM_PREFERENCES_DIRECTORY, &path) == B_OK)
		_ScanDirectory(path.Path(), true, 0);
	if (find_directory(B_USER_APPS_DIRECTORY, &path) == B_OK)
		_ScanDirectory(path.Path(), false, 0);
	if (find_directory(B_USER_PREFERENCES_DIRECTORY, &path) == B_OK)
		_ScanDirectory(path.Path(), true, 0);

	// App non-packaged (verificare par. 9.6: le costanti).
	if (find_directory(B_SYSTEM_NONPACKAGED_DIRECTORY, &path) == B_OK) {
		path.Append("apps");
		_ScanDirectory(path.Path(), false, 0);
	}
	if (find_directory(B_USER_NONPACKAGED_DIRECTORY, &path) == B_OK) {
		path.Append("apps");
		_ScanDirectory(path.Path(), false, 0);
	}
}

void
DeskbarWindow::_ScanDirectory(const char* dirPath, bool prefs, int depth)
{
	BDirectory dir(dirPath);
	if (dir.InitCheck() != B_OK)
		return;

	BEntry entry;
	while (dir.GetNextEntry(&entry, false) == B_OK) {
		entry_ref ref;
		if (entry.GetRef(&ref) != B_OK)
			continue;

		BEntry target(&ref, true);
		if (target.InitCheck() != B_OK || !target.Exists())
			continue;

		if (target.IsDirectory()) {
			// Una sola sottocartella di profondita' per le app.
			if (!prefs && depth < 1 && entry.IsDirectory()) {
				BPath sub;
				if (entry.GetPath(&sub) == B_OK)
					_ScanDirectory(sub.Path(), false, depth + 1);
			}
			continue;
		}

		// Solo i file con firma di applicazione.
		entry_ref targetRef;
		if (target.GetRef(&targetRef) != B_OK)
			continue;
		BFile file(&targetRef, B_READ_ONLY);
		if (file.InitCheck() != B_OK)
			continue;
		BAppFileInfo appInfo(&file);
		char signature[B_MIME_TYPE_LENGTH];
		if (appInfo.GetSignature(signature) != B_OK || signature[0] == '\0')
			continue;

		_CacheEnsure(ref, prefs, true);
	}
}

bool
DeskbarWindow::_FavoritesPath(BPath* path)
{
	// Interoperabile col menu della Deskbar vera.
	if (find_directory(B_USER_DESKBAR_DIRECTORY, path) != B_OK)
		return false;
	return path->Append("Favorites") == B_OK;
}

void
DeskbarWindow::_RefreshFavorites()
{
	fFavorites.clear();

	BPath favPath;
	if (!_FavoritesPath(&favPath))
		return;
	BDirectory dir(favPath.Path());
	if (dir.InitCheck() != B_OK)
		return;

	BEntry entry;
	while (dir.GetNextEntry(&entry, false) == B_OK) {
		entry_ref ref;
		if (entry.GetRef(&ref) != B_OK)
			continue;
		BPath linkPath;
		if (entry.GetPath(&linkPath) != B_OK)
			continue;

		BEntry target(&ref, true);
		node_ref node;
		if (target.InitCheck() != B_OK || !target.Exists()
			|| target.GetNodeRef(&node) != B_OK)
			continue;

		fFavorites[node] = linkPath.Path();
		_CacheEnsure(ref, false, false);
	}
}

void
DeskbarWindow::_RefreshRecents()
{
	fRecents.clear();
	int32 position = 0;

	for (int category = 0; category < 3; category++) {
		BMessage list;
		if (category == 0)
			be_roster->GetRecentApps(&list, 10);
		else if (category == 1)
			be_roster->GetRecentDocuments(&list, 10);
		else
			be_roster->GetRecentFolders(&list, 10);

		int32 count = 0;
		list.GetInfo("refs", NULL, &count);
		for (int32 i = 0; i < count; i++) {
			entry_ref ref;
			if (list.FindRef("refs", i, &ref) != B_OK)
				continue;
			BEntry entry(&ref, true);
			node_ref node;
			if (entry.InitCheck() != B_OK || !entry.Exists()
				|| entry.GetNodeRef(&node) != B_OK)
				continue;
			if (category == 2 && !entry.IsDirectory())
				continue;
			if (fRecents.find(node) != fRecents.end())
				continue;

			fRecents[node] = position++;
			_CacheEnsure(ref, false, true);
		}
	}
}

uint32
DeskbarWindow::_IndexFlagsFor(const node_ref& node) const
{
	uint32 flags = 0;
	if (fFavorites.find(node) != fFavorites.end())
		flags |= kFlagFavorite;
	if (fRecents.find(node) != fRecents.end())
		flags |= kFlagRecent;
	return flags;
}

void
DeskbarWindow::_ApplyIndexFlags(UnifiedListItem* item)
{
	uint32 flags = item->ItemFlags() & ~(kFlagFavorite | kFlagRecent);
	item->SetItemFlags(flags | _IndexFlagsFor(item->Node()));
}

// Aggiorna i flag Favorite/Recent di tutti gli item vivi.
void
DeskbarWindow::_RefreshFlags()
{
	for (auto& pair : fCache)
		_ApplyIndexFlags(pair.second);
	for (BListItem* item : fTransient) {
		UnifiedListItem* u = dynamic_cast<UnifiedListItem*>(item);
		if (u != NULL && u->Kind() != kKindVolume)
			_ApplyIndexFlags(u);
	}
}

int32
DeskbarWindow::_RecentPosition(const UnifiedListItem* item) const
{
	auto it = fRecents.find(item->Node());
	return it != fRecents.end() ? it->second : 0x7fffffff;
}

// Piano par. 3.4.
void
DeskbarWindow::_ToggleFavorite(const entry_ref& ref)
{
	BEntry target(&ref, true);
	node_ref node;
	if (target.InitCheck() != B_OK || target.GetNodeRef(&node) != B_OK)
		return;

	auto it = fFavorites.find(node);
	if (it != fFavorites.end()) {
		BEntry link(it->second.String());
		if (link.InitCheck() == B_OK)
			link.Remove();
		fFavorites.erase(it);
	} else {
		BPath favPath;
		if (!_FavoritesPath(&favPath))
			return;
		_CreateSymlink(ref, favPath.Path());
		_RefreshFavorites();
	}
	_RefreshFlags();

	bool needRebuild = fState.world == kWorldCatalog
		&& ((fState.tags & kTagFavorite) != 0 || _IsGrouped());
	if (needRebuild) {
		_Rebuild();
		_SelectNode(node);
	} else {
		fMainList->Invalidate();
	}
}

void
DeskbarWindow::_SelectNode(const node_ref& node)
{
	for (int32 i = 0; i < fMainList->CountItems(); i++) {
		UnifiedListItem* item
			= dynamic_cast<UnifiedListItem*>(fMainList->ItemAt(i));
		if (item != NULL && item->Node() == node) {
			_SelectIndex(i, false);
			return;
		}
	}
}

void
DeskbarWindow::_AddToGoTo(const entry_ref& ref)
{
	BPath userSettings;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &userSettings) != B_OK)
		return;
	userSettings.Append("Tracker/Go");
	_CreateSymlink(ref, userSettings.Path());

	if (fState.world == kWorldGo)
		_Rebuild();
}

void
DeskbarWindow::_CreateSymlink(const entry_ref& ref, const char* targetDir)
{
	BDirectory dir(targetDir);
	if (dir.InitCheck() != B_OK) {
		create_directory(targetDir, 0755);
		dir.SetTo(targetDir);
	}

	BEntry entry(&ref);
	BPath sourcePath;
	entry.GetPath(&sourcePath);

	BString linkName(ref.name);
	BPath linkPath(targetDir);
	linkPath.Append(linkName.String());

	int counter = 1;
	while (BEntry(linkPath.Path()).Exists()) {
		linkName = ref.name;
		linkName << " " << counter++;
		linkPath.SetTo(targetDir);
		linkPath.Append(linkName.String());
	}

	dir.CreateSymLink(linkName.String(), sourcePath.Path(), NULL);
}

void
DeskbarWindow::_RebuildCatalog()
{
	std::vector<UnifiedListItem*> matching;
	for (auto& pair : fCache) {
		pair.second->SetHideRecent(false);
		if (Matches(pair.second, fState, fSettings.options, true))
			matching.push_back(pair.second);
	}

	auto byName = [](const UnifiedListItem* a, const UnifiedListItem* b) {
		int cmp = CompareNames(a->Name(), b->Name());
		if (cmp != 0)
			return cmp < 0;
		return NodeRefLess()(a->Node(), b->Node());
	};

	_ClearList();
	fMainList->SetStarsEnabled(true);

	if (_IsGrouped()) {
		// Ogni elemento una sola volta, nella prima sezione che lo comprende.
		std::vector<UnifiedListItem*> favorites, recents, others;
		for (UnifiedListItem* item : matching) {
			if (item->ItemFlags() & kFlagFavorite)
				favorites.push_back(item);
			else if (item->ItemFlags() & kFlagRecent)
				recents.push_back(item);
			else
				others.push_back(item);
		}
		std::sort(favorites.begin(), favorites.end(), byName);
		std::sort(recents.begin(), recents.end(), byName);
		std::sort(others.begin(), others.end(), byName);

		// Nella sezione Recenti l'orologio sarebbe ripetuto su ogni riga.
		for (UnifiedListItem* item : recents)
			item->SetHideRecent(true);

		_AddSection(B_TRANSLATE("Favorites"), favorites);
		_AddSection(B_TRANSLATE_COMMENT("Recent",
			"Filter chip and section header"), recents);
		_AddSection(B_TRANSLATE_COMMENT("Others", "Section header"), others);
	} else if (fState.tags == kTagRecent) {
		// Solo Recent: ordine di recenza, orologio superfluo.
		std::sort(matching.begin(), matching.end(),
			[this](const UnifiedListItem* a, const UnifiedListItem* b) {
				return _RecentPosition(a) < _RecentPosition(b);
			});
		for (UnifiedListItem* item : matching) {
			item->SetHideRecent(true);
			fMainList->AddItem(item);
		}
	} else {
		std::sort(matching.begin(), matching.end(), byName);
		for (UnifiedListItem* item : matching)
			fMainList->AddItem(item);
	}

	bool filtered = fState.kindMask != 0 || fState.tags != 0
		|| !fState.text.IsEmpty();
	fMainList->SetEmptyMessage(filtered ? B_TRANSLATE("No matches")
		: B_TRANSLATE("<empty>"));
	_UpdateCounter((int32)matching.size(), 0);
	_SelectFirst();
}

void
DeskbarWindow::_RebuildGo()
{
	_ClearList();
	fMainList->SetStarsEnabled(false);

	std::vector<UnifiedListItem*> items;
	BPath goPath;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &goPath) == B_OK) {
		goPath.Append("Tracker/Go");
		_ScanLinkDirectory(goPath.Path(), items);
	}

	auto byName = [](const UnifiedListItem* a, const UnifiedListItem* b) {
		return CompareNames(a->Name(), b->Name()) < 0;
	};
	std::sort(items.begin(), items.end(), byName);

	// In Go contano solo il testo: tipo e tag non si applicano.
	FacetState textOnly;
	textOnly.text = fState.text;
	ViewOptions noOptions;
	noOptions.hideSystem = false;

	int32 count = 0;
	for (UnifiedListItem* item : items) {
		if (Matches(item, textOnly, noOptions, false)) {
			fTransient.push_back(item);
			fMainList->AddItem(item);
			count++;
		} else {
			delete item;
		}
	}

	fMainList->SetEmptyMessage(fState.text.IsEmpty()
		? B_TRANSLATE("<empty>") : B_TRANSLATE("No matches"));
	_UpdateCounter(count, 0);
	_SelectFirst();
}

// Elenca i symlink di una cartella (Go) creando item transitori.
void
DeskbarWindow::_ScanLinkDirectory(const char* dirPath,
	std::vector<UnifiedListItem*>& out)
{
	BDirectory dir(dirPath);
	if (dir.InitCheck() != B_OK)
		return;

	BEntry entry;
	while (dir.GetNextEntry(&entry, false) == B_OK) {
		entry_ref ref;
		if (entry.GetRef(&ref) != B_OK)
			continue;
		BPath linkPath;
		if (entry.GetPath(&linkPath) != B_OK)
			continue;
		UnifiedListItem* item
			= CreateItemForRef(ref, false, false, linkPath.Path());
		if (item != NULL)
			out.push_back(item);
	}
}
