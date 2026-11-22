/*
 * SpielBar v3 - la finestra principale
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#include <AppDefs.h>
#include <Application.h>
#include <Catalog.h>
#include <Directory.h>
#include <Entry.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <Path.h>
#include <Point.h>
#include <PopUpMenu.h>
#include <Rect.h>
#include <Roster.h>
#include <String.h>
#include <SupportDefs.h>
#include <UnicodeChar.h>

#include "DeskbarWindow.h"
#include "SpielBarDefs.h"
#include "SpielBarTypes.h"
#include "MimeCatalog.h"
#include "TrackerBridge.h"
#include "Emblems.h"
#include "ToolbarButtons.h"
#include "ListItems.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


bool
DeskbarWindow::_HandleListKey(BMessage* message, char key)
{
	switch (key) {
		case B_ENTER:
		{
			if (fMainList->CurrentSelection() < 0) {
				int32 first = _NextSelectable(-1, 1);
				if (first >= 0)
					fMainList->Select(first);
			}
			int32 modifiers = 0;
			message->FindInt32("modifiers", &modifiers);
			_LaunchSelectedItem((modifiers & B_SHIFT_KEY) != 0);
			return true;
		}

		case B_DOWN_ARROW:
		{
			int32 next = _NextSelectable(fMainList->CurrentSelection(), 1);
			if (next >= 0)
				_SelectIndex(next, true);
			return true;
		}

		case B_UP_ARROW:
		{
			int32 current = fMainList->CurrentSelection();
			if (current < 0)
				return true;
			int32 prev = _NextSelectable(current, -1);
			if (prev >= 0)
				_SelectIndex(prev, true);
			return true;
		}

		case B_PAGE_UP:
		{
			int32 first = _NextSelectable(-1, 1);
			if (first >= 0)
				_SelectIndex(first, true);
			return true;
		}

		case B_PAGE_DOWN:
		{
			int32 last = _NextSelectable(fMainList->CountItems(), -1);
			if (last >= 0)
				_SelectIndex(last, true);
			return true;
		}

		case B_ESCAPE:
			_HandleEscape();
			return true;

		case B_FUNCTION_KEY:
		{
			int32 fkey;
			if (message->FindInt32("key", &fkey) == B_OK && fkey == B_F10_KEY) {
				int32 modifiers = 0;
				message->FindInt32("modifiers", &modifiers);
				if (modifiers & B_SHIFT_KEY) {
					_OpenWorldMenu();
					return true;
				}
				int32 selected = fMainList->CurrentSelection();
				if (selected >= 0) {
					BRect itemRect = fMainList->ItemFrame(selected);
					BPoint where(itemRect.left + 10, itemRect.bottom);

					BMessage contextMsg(kMsgOpenMenu);
					contextMsg.AddPoint("where", where);
					contextMsg.AddInt32("index", selected);
					contextMsg.AddBool("keep_open",
						(modifiers & B_SHIFT_KEY) != 0);
					PostMessage(&contextMsg);
				}
				return true;
			}
			return false;
		}
	}
	return false;
}

// Esc: cancella il testo; altrimenti torna al Catalogo; altrimenti
// azzera le faccette.
void
DeskbarWindow::_HandleEscape()
{
	if (fFilter->Text()[0] != '\0') {
		fFilter->SetText("");	// il messaggio di modifica ricostruisce
		return;
	}
	if (fState.world != kWorldCatalog) {
		FacetState st = fState;
		st.world = kWorldCatalog;
		_SetState(st, kFromUser);
		return;
	}
	if (fState.kindMask != 0 || fState.tags != 0) {
		FacetState st = fState;
		st.kindMask = 0;
		st.tags = 0;
		_SetState(st, kFromUser);
		return;
	}
	PostMessage(kRaiseQuit);	// niente da azzerare: chiude
}

void
DeskbarWindow::_ToggleWorldKey(World world)
{
	BMessage msg(kMsgToggleWorld);
	msg.AddInt32("world", world);
	PostMessage(&msg);
}

// Menu del pulsante del mondo attivo (Shift+F10): Dischi, Go o Cerca.
void
DeskbarWindow::_OpenWorldMenu()
{
	IconButton* button = NULL;
	switch (fState.world) {
		case kWorldDisks:
			button = fDisksBtn;
			break;
		case kWorldGo:
			button = fGoBtn;
			break;
		case kWorldSearch:
			button = fQueryBtn;
			break;
		default:
			break;
	}
	if (button != NULL)
		button->InvokeMenuAtButton(false);
}

bool
DeskbarWindow::_HandleCommandKey(char key, int32 modifiers)
{
	switch (key) {
		case ',':
			// Menu Vista: contiene le impostazioni di SpielBar.
			fViewBtn->InvokePrimaryAction();
			return true;

		case 'q': case 'Q':
			// Spegnimento: solo con Shift, per non confonderlo con "esci".
			if (modifiers & B_SHIFT_KEY) {
				PostMessage(kMsgAskHalt);
				return true;
			}
			return false;

		case 'w': case 'W':
			// Menu del pulsante di spegnimento (Power Off, Restart System).
			if (modifiers & B_SHIFT_KEY) {
				fShutdownBtn->InvokeMenuAtButton(true);
				return true;
			}
			return false;

		case 'd': case 'D':
			_ToggleWorldKey(kWorldDisks);
			return true;
		case 'g': case 'G':
			_ToggleWorldKey(kWorldGo);
			return true;
		case 'f': case 'F':
			_ToggleWorldKey(kWorldSearch);
			return true;

		case '1': case '2': case '3': case '4':
		{
			// (verificare par. 9.15: collisioni con i workspace)
			static const uint32 kinds[4]
				= { kKindApp, kKindPreflet, kKindFile, kKindFolder };
			BMessage msg(kMsgToggleKind);
			msg.AddInt32("kind", (int32)kinds[key - '1']);
			PostMessage(&msg);
			return true;
		}

		case 'j': case 'J':
		case 's': case 'S':
		{
			BMessage msg(kMsgSetTag);
			msg.AddInt32("tag", (int32)((key == 'j' || key == 'J')
				? kTagRecent : kTagFavorite));
			PostMessage(&msg);
			return true;
		}

		case 'b': case 'B':
		{
			UnifiedListItem* item = _SelectedItem();
			if (item != NULL && item->Kind() != kKindVolume
				&& fMainList->StarsEnabled()) {
				BMessage msg(kMsgToggleFavorite);
				msg.AddRef("refs", &item->Ref());
				PostMessage(&msg);
			}
			return true;
		}

		case 'i': case 'I':
		{
			UnifiedListItem* item = _SelectedItem();
			if (item != NULL) {
				BPath fullPath(&item->Ref());
				OpenInfo(fullPath.Path());
			}
			return true;
		}

		case 'r': case 'R':
		{
			UnifiedListItem* item = _SelectedItem();
			if (item != NULL)
				RevealInTracker(item->Ref());
			return true;
		}
	}
	return false;
}

// ---- Lancio e menu contestuale ----------------------------------------
// Unico punto in cui un lancio decide il destino della finestra: chiuderla
// (comportamento storico) oppure restare aperta, se il lancio e' avvenuto con
// Shift premuto. Restare aperti significa anche ignorare la disattivazione
// che l'app appena avviata provoca prendendo il focus.
void
DeskbarWindow::_FinishLaunch(bool keepOpen)
{
	if (keepOpen)
		fKeepOpenAfterLaunch = true;
	else
		PostMessage(kRaiseQuit);
}

void
DeskbarWindow::_LaunchSelectedItem(bool keepOpen)
{
	int32 index = fMainList->CurrentSelection();
	if (index < 0)
		return;

	if (VolumeListItem* volItem
			= dynamic_cast<VolumeListItem*>(fMainList->ItemAt(index))) {
		if (volItem->IsMounted()) {
			// Un disco montato si apre; si smonta solo dal menu contestuale.
			if (!volItem->Path().IsEmpty()) {
				OpenFolderInTracker(volItem->Path().String());
				_FinishLaunch(keepOpen);
			}
		} else {
			// Un disco non montato viene montato.
			BMessage mountMsg(kToggleVolume);
			mountMsg.AddInt32("partition_id", volItem->PartitionId());
			mountMsg.AddBool("mount", true);
			PostMessage(&mountMsg);
		}
		return;
	}

	UnifiedListItem* item = dynamic_cast<UnifiedListItem*>(fMainList->ItemAt(index));
	if (item == NULL)	// intestazione
		return;

	BEntry entry(&item->Ref(), true);
	if (!entry.Exists())
		return;

	if (entry.IsDirectory()) {
		BMessenger tracker("application/x-vnd.Be-TRAK");
		if (tracker.IsValid()) {
			BMessage openMsg(B_REFS_RECEIVED);
			openMsg.AddRef("refs", &item->Ref());
			tracker.SendMessage(&openMsg);
			_FinishLaunch(keepOpen);
		}
	} else {
		if (!item->Signature().IsEmpty())
			be_roster->Launch(item->Signature().String());
		else
			be_roster->Launch(&item->Ref());
		_FinishLaunch(keepOpen);
	}
}

void
DeskbarWindow::_ShowContextMenu(BPoint where, int32 index, bool keepOpen)
{
	if (VolumeListItem* volItem
			= dynamic_cast<VolumeListItem*>(fMainList->ItemAt(index))) {
		_ShowVolumeMenu(where, volItem);
		return;
	}

	UnifiedListItem* item = dynamic_cast<UnifiedListItem*>(fMainList->ItemAt(index));
	if (item == NULL)	// le intestazioni non hanno menu
		return;

	BPopUpMenu* menu = new BPopUpMenu("context_menu", false, false);

	BMessage* launchMsg = new BMessage(kMsgLaunch);
	launchMsg->AddBool("keep_open", keepOpen);	// Shift: non chiudere
	menu->AddItem(new BMenuItem(B_TRANSLATE("Launch"), launchMsg));

	BMessage* showMsg = new BMessage(kMsgRevealInTracker);
	showMsg->AddRef("refs", &item->Ref());
	menu->AddItem(new BMenuItem(B_TRANSLATE("Reveal in Tracker"), showMsg, 'R'));

	BMessage* infoMsg = new BMessage(kMsgGetInfo);
	infoMsg->AddRef("refs", &item->Ref());
	menu->AddItem(new BMenuItem(B_TRANSLATE("Get Info"), infoMsg, 'I'));

	// Per le voci installate da un pacchetto: mostra il .hpkg in Tracker.
	if (item->ItemFlags() & (kFlagPackaged | kFlagSystem)) {
		entry_ref packageRef;
		if (FindOriginPackage(item->Ref(), &packageRef)) {
			BMessage* pkgMsg = new BMessage(kMsgRevealInTracker);
			pkgMsg->AddRef("refs", &packageRef);
			menu->AddItem(new BMenuItem(B_TRANSLATE("Reveal origin package"),
				pkgMsg));
		}
	}

	if (fState.world == kWorldCatalog || fState.world == kWorldSearch) {
		menu->AddSeparatorItem();

		bool isFavorite = (item->ItemFlags() & kFlagFavorite) != 0;
		BMessage* favMsg = new BMessage(kMsgToggleFavorite);
		favMsg->AddRef("refs", &item->Ref());
		menu->AddItem(new BMenuItem(isFavorite
			? B_TRANSLATE("Remove from favorites")
			: B_TRANSLATE("Add to favorites"), favMsg, 'B'));

		if (item->Kind() == kKindFolder) {
			BMessage* addGoMsg = new BMessage(kMsgAddGoTo);
			addGoMsg->AddRef("refs", &item->Ref());
			menu->AddItem(new BMenuItem(B_TRANSLATE("Add to Go"), addGoMsg));
		}
	}

	if (fState.world == kWorldGo && !item->SymlinkPath().IsEmpty()) {
		menu->AddSeparatorItem();
		BMessage* removeMsg = new BMessage(kMsgRemoveItem);
		removeMsg->AddString("symlink_path", item->SymlinkPath());
		removeMsg->AddInt32("index", index);
		menu->AddItem(new BMenuItem(B_TRANSLATE("Remove from Go"),
			removeMsg));
	}

	SetMenuTarget(menu, this);
	menu->Go(fMainList->ConvertToScreen(where), true, false, true);
}
