/*
 * SpielBar v3 - la finestra principale
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#include <Application.h>
#include <Catalog.h>
#include <Entry.h>
#include <Locale.h>
#include <Message.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <Node.h>
#include <Point.h>
#include <Query.h>
#include <String.h>
#include <SupportDefs.h>
#include <memory>

#include "DeskbarWindow.h"
#include "SpielBarDefs.h"
#include "SpielBarTypes.h"
#include "Collation.h"
#include "ListItems.h"
#include "ItemFactory.h"
#include "SearchQuery.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


// ---- Cerca (piano par. 6) ---------------------------------------------
void
DeskbarWindow::_ScheduleSearch()
{
	delete fDebounceRunner;
	BMessage delayed(kMsgDebouncedFilter);
	fDebounceRunner = new BMessageRunner(this, &delayed, kDebounceTime, 1);
}

void
DeskbarWindow::_StartSearch()
{
	delete fDebounceRunner;
	fDebounceRunner = nullptr;

	uint32 generation = ++(*fSearchGen);
	_ClearList();
	fMainList->SetStarsEnabled(true);
	fSearchCount = 0;
	fSearchTruncated = false;
	fSearchDone = false;
	fUserMovedSelection = false;
	_ClearCounter();

	if (fState.text.CountChars() < 3) {
		fMainList->SetEmptyMessage(B_TRANSLATE("Type at least 3 characters"));
		fSearchDone = true;
		return;
	}

	fMainList->SetEmptyMessage(B_TRANSLATE("Searching" B_UTF8_ELLIPSIS));

	SearchParams* params = new SearchParams;
	params->target = BMessenger(this);
	params->generation = fSearchGen;
	params->myGeneration = generation;
	params->text = fState.text;

	thread_id thread = spawn_thread(SearchWorker, "search_worker",
		B_LOW_PRIORITY, params);
	if (thread >= 0) {
		resume_thread(thread);
	} else {
		delete params;
		fMainList->SetEmptyMessage(B_TRANSLATE("No matches"));
	}
}

void
DeskbarWindow::_HandleSearchResults(BMessage* msg)
{
	int32 gen;
	if (msg->FindInt32("gen", &gen) != B_OK
		|| (uint32)gen != fSearchGen->load()
		|| fState.world != kWorldSearch)
		return;	// risultati di una stringa superata

	// Filtro per tipo e per "Hide system apps"; testo e tag non servono.
	FacetState filter;
	filter.kindMask = fState.kindMask;

	entry_ref ref;
	for (int32 i = 0; msg->FindRef("refs", i, &ref) == B_OK; i++) {
		UnifiedListItem* item = CreateItemForRef(ref, false, true, NULL);
		if (item == NULL)
			continue;
		if (!Matches(item, filter, fSettings.options, false)) {
			delete item;
			continue;
		}
		_ApplyIndexFlags(item);
		fTransient.push_back(item);

		// Inserimento binario per nome.
		int32 lo = 0;
		int32 hi = fMainList->CountItems();
		while (lo < hi) {
			int32 mid = (lo + hi) / 2;
			UnifiedListItem* existing
				= dynamic_cast<UnifiedListItem*>(fMainList->ItemAt(mid));
			if (existing != NULL
				&& CompareNames(existing->Name(), item->Name()) <= 0)
				lo = mid + 1;
			else
				hi = mid;
		}
		fMainList->AddItem(item, lo);
		fSearchCount++;
	}

	if (!fUserMovedSelection) {
		int32 first = _NextSelectable(-1, 1);
		if (first >= 0) {
			fMainList->Select(first);
			fMainList->ScrollTo(BPoint(0, 0));
		}
	}
	_UpdateSearchCounter();
}

void
DeskbarWindow::_UpdateSearchCounter()
{
	if (fSearchCount == 0)
		_ClearCounter();
	else
		_UpdateCounter(fSearchCount, fSearchTruncated ? 2 : 1);
}
