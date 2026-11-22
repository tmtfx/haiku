/*
 * SpielBar v3 - la finestra principale
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#include <AppDefs.h>
#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Deskbar.h>
#include <Directory.h>
#include <DiskDeviceList.h>
#include <Entry.h>
#include <GroupLayout.h>
#include <Handler.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <Locale.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <Partition.h>
#include <Path.h>
#include <Point.h>
#include <Roster.h>
#include <RosterPrivate.h>
#include <Screen.h>
#include <ScrollView.h>
#include <String.h>
#include <StringView.h>
#include <SupportDefs.h>
#include <TextControl.h>
#include <Volume.h>
#include <Window.h>
#include <math.h>
#include <stdio.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <new>
#include <vector>

#include "DeskbarWindow.h"
#include "SpielBarDefs.h"
#include "SpielBarTypes.h"
#include "Collation.h"
#include "Formatting.h"
#include "SettingsStore.h"
#include "TrackerBridge.h"
#include "DeskbarLayout.h"
#include "ListItems.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


DeskbarWindow::DeskbarWindow(const FacetState* startOverride)
	: BWindow(InitialWindowFrame(), "Deskbar",
		B_BORDERED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_NOT_H_RESIZABLE | (kNotMovable ? B_NOT_MOVABLE : 0)
			| B_QUIT_ON_WINDOW_CLOSE
			| B_AUTO_UPDATE_SIZE_LIMITS)
	, fCloseRunner(NULL)
	, fDebounceRunner(nullptr)
	, fKeepOpenAfterLaunch(false)
	, fSearchCount(0)
	, fSearchTruncated(false)
	, fSearchDone(false)
	, fUserMovedSelection(false)
{
	SetWorkspaces(B_ALL_WORKSPACES);
	fControlsOnTop = IsDeskbarAtTop();
	fDragActive = false;
	fDragHeight = 0;
	fPendingHeight = 0;
	for (int i = 0; i < 3; i++)
		fGhost[i] = NULL;
	InitCollator();
	SettingsLoad(fSettings);
	fSearchGen = std::make_shared<std::atomic<uint32> >(0);

	_BuildInterface();

	// Indici e cache (processo breve: niente watch_node, piano par. 3.3).
	_ScanCatalog();
	_RefreshFavorites();
	_RefreshRecents();
	_RefreshFlags();

	// Vista di avvio, applicata prima della prima ricostruzione:
	// argv, poi modalita' (Saved o Last), poi Default (piano par. 3.6).
	FacetState initial;
	if (startOverride != NULL) {
		initial = *startOverride;
	} else if (fSettings.startupMode == kStartSaved
		&& fSettings.hasSavedView) {
		initial.world = fSettings.savedView.world;
		initial.kindMask = fSettings.savedView.kindMask;
		initial.tags = fSettings.savedView.tags;
	} else if (fSettings.startupMode == kStartLast) {
		initial.world = fSettings.lastView.world;
		initial.kindMask = fSettings.lastView.kindMask;
		initial.tags = fSettings.lastView.tags;
	}
	_SetState(initial, kFromArgs, true);

	fFilter->MakeFocus(true);

	BMessage reply;
	BMessenger msngr("application/x-vnd.Be-TSKB"); // la deskbar
	msngr.SendMessage(kPressDeskbarBtn, &reply);
}

DeskbarWindow::~DeskbarWindow()
{
	_DestroyGhost();

	// Ferma un'eventuale ricerca in corso: il worker non tocca la finestra.
	++(*fSearchGen);
	delete fDebounceRunner;
	delete fCloseRunner;

	fMainList->MakeEmpty();
	_FreeTransient();
	for (auto& pair : fCache)
		delete pair.second;
	fCache.clear();
	for (BListItem* item : fVolumesCache)
		delete item;
	fVolumesCache.clear();
}

void
DeskbarWindow::WindowActivated(bool active)
{
	BWindow::WindowActivated(active);
	if (kDebugLayout)
		fprintf(stderr, "SpielBar: WindowActivated(%d)\n", (int)active);
	if (!active) {
		// Trascinamento interrotto: niente contorno residuo, hover di nuovo
		// attivo.
		_DestroyGhost();
		fDragActive = false;
		fMainList->SetHoverEnabled(true);
	}

	if (!active) {
		delete fCloseRunner;
		fCloseRunner = NULL;
		BMessage reply;
		BMessenger msngr("application/x-vnd.Be-TSKB");
		msngr.SendMessage(kRaiseDeskbarBtn, &reply);
		if (fKeepOpenAfterLaunch) {
			// Lancio con Shift: l'app appena avviata ha preso il focus, ma
			// la finestra resta aperta per lanciare altro. Il permesso vale
			// una volta sola: la prossima disattivazione chiude come sempre.
			fKeepOpenAfterLaunch = false;
		} else {
			BMessage closeMsg(kMsgDelayedClose);
			fCloseRunner = new BMessageRunner(this, &closeMsg, 50000, 1);
		}
	} else {
		BPoint targetPoint = CalculateAppPosition(Bounds());
		MoveTo(targetPoint);
		delete fCloseRunner;
		fCloseRunner = NULL;
		fKeepOpenAfterLaunch = false;
	}
}

void
DeskbarWindow::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kRaiseQuit:
		{
			BMessage reply;
			BMessenger msngr("application/x-vnd.Be-TSKB");
			msngr.SendMessage(kRaiseDeskbarBtn, &reply);
			PostMessage(B_QUIT_REQUESTED);
			break;
		}

		case kMsgDelayedClose:
			if (!IsActive())
				PostMessage(kRaiseQuit);
			break;

		// ---- Stato a faccette ----

		case kMsgSetState:
		{
			FacetState st = fState;
			int32 value;
			if (msg->FindInt32("world", &value) == B_OK)
				st.world = (World)value;
			if (msg->FindInt32("kinds", &value) == B_OK)
				st.kindMask = (uint32)value;
			if (msg->FindInt32("tag", &value) == B_OK)
				st.tags = (uint32)value & kTagFilterMask;
			int32 source = kFromUser;
			msg->FindInt32("source", &source);
			_SetState(st, source);
			break;
		}

		case kMsgToggleWorld:
		{
			int32 world;
			if (msg->FindInt32("world", &world) != B_OK)
				break;
			FacetState st = fState;
			st.world = (st.world == (World)world)
				? kWorldCatalog : (World)world;
			_SetState(st, kFromUser);
			fFilter->MakeFocus(true);
			break;
		}

		case kMsgToggleKind:
		{
			int32 kind;
			if (msg->FindInt32("kind", &kind) != B_OK || !_HasFacets())
				break;
			FacetState st = fState;
			st.kindMask ^= (uint32)kind;
			_SetState(st, kFromUser);
			fFilter->MakeFocus(true);
			break;
		}

		case kMsgSetTag:
		{
			// Recent e Preferiti si combinano in OR: ogni clic inverte un bit.
			int32 bit;
			if (msg->FindInt32("tag", &bit) != B_OK
				|| fState.world != kWorldCatalog)
				break;
			FacetState st = fState;
			st.tags ^= ((uint32)bit & kTagFilterMask);
			_SetState(st, kFromUser);
			fFilter->MakeFocus(true);
			break;
		}

		case kMsgApplyFilter:
		{
			fFilter->Invalidate();	// segnaposto
			BString text = fFilter->Text();
			if (text == fState.text)
				break;
			fState.text = text;
			fUserMovedSelection = false;
			if (fState.world == kWorldSearch)
				_ScheduleSearch();
			else
				_Rebuild();
			break;
		}

		case kMsgDebouncedFilter:
			delete fDebounceRunner;
			fDebounceRunner = nullptr;
			if (fState.world == kWorldSearch)
				_Rebuild();
			break;

		// ---- Opzioni di vista e vista di avvio ----

		case kMsgOptGroup:
			fSettings.options.groupByTag = !fSettings.options.groupByTag;
			SettingsSave(fSettings);
			if (fState.world == kWorldCatalog)
				_Rebuild();
			break;

		case kMsgOptHideSystem:
			fSettings.options.hideSystem = !fSettings.options.hideSystem;
			SettingsSave(fSettings);
			if (fState.world == kWorldCatalog || fState.world == kWorldSearch)
				_Rebuild();
			break;

		case kMsgStartupMode:
		{
			int32 mode;
			if (msg->FindInt32("mode", &mode) == B_OK
				&& mode >= kStartDefault && mode <= kStartSaved) {
				if (mode == kStartSaved && !fSettings.hasSavedView)
					break;
				fSettings.startupMode = (StartupMode)mode;
				if (mode == kStartLast)
					_StoreCurrentAs(fSettings.lastView);
				SettingsSave(fSettings);
			}
			break;
		}

		case kMsgSaveStartupView:
			_StoreCurrentAs(fSettings.savedView);
			fSettings.hasSavedView = true;
			fSettings.startupMode = kStartSaved;
			SettingsSave(fSettings);
			break;

		case kMsgDeskbarPrefs:
			be_roster->Launch("application/x-vnd.Haiku-DeskbarPreferences");
			break;

		// ---- Preferiti e Go ----

		case kMsgToggleFavorite:
		{
			entry_ref ref;
			if (msg->FindRef("refs", &ref) == B_OK)
				_ToggleFavorite(ref);
			break;
		}

		case kMsgAddGoTo:
		{
			entry_ref ref;
			if (msg->FindRef("refs", &ref) == B_OK)
				_AddToGoTo(ref);
			break;
		}

		case kMsgRemoveItem:
		{
			BString symlinkPath;
			int32 itemIndex;
			if (msg->FindString("symlink_path", &symlinkPath) == B_OK
				&& msg->FindInt32("index", &itemIndex) == B_OK) {
				BEntry symlink(symlinkPath.String());
				if (symlink.InitCheck() == B_OK && symlink.Exists())
					symlink.Remove();

				if (itemIndex >= 0 && itemIndex < fMainList->CountItems()) {
					BListItem* item = fMainList->RemoveItem(itemIndex);
					// Gli item transitori sono posseduti da fTransient.
					auto it = std::find(fTransient.begin(),
						fTransient.end(), item);
					if (it != fTransient.end())
						fTransient.erase(it);
					delete item;
					fMainList->ResetHover();

					int32 newSelection = itemIndex;
					if (newSelection >= fMainList->CountItems()
						&& newSelection > 0)
						newSelection--;
					if (newSelection < fMainList->CountItems())
						_SelectIndex(newSelection, false);
					_UpdateCounter(fMainList->CountItems(), 0);
					fMainList->Invalidate();
				}
			}
			break;
		}

		// ---- Lancio e menu ----

		case kMsgLaunch:
		{
			// "keep_open" arriva dai clic con Shift (lista e menu
			// contestuale): lancia senza chiudere.
			bool keepOpen = false;
			msg->FindBool("keep_open", &keepOpen);
			_LaunchSelectedItem(keepOpen);
			break;
		}

		case kMsgOpenMenu:
		{
			BPoint where;
			int32 index;
			bool keepOpen = false;
			msg->FindBool("keep_open", &keepOpen);
			if (msg->FindPoint("where", &where) == B_OK
				&& msg->FindInt32("index", &index) == B_OK) {
				_ShowContextMenu(where, index, keepOpen);
			}
			break;
		}

		case kMsgGetInfo:
		{
			entry_ref ref;
			if (msg->FindRef("refs", &ref) == B_OK) {
				BPath path(&ref);
				OpenInfo(path.Path());
			}
			break;
		}

		case kMsgRevealInTracker:
		{
			entry_ref ref;
			if (msg->FindRef("refs", &ref) == B_OK)
				RevealInTracker(ref);
			break;
		}

		case kMsgShutDown:
		{
			BRoster r;
			BRoster::Private rp(r);
			rp.ShutDown(false, false, true);
			break;
		}

		case kMsgReboot:
		{
			BRoster r;
			BRoster::Private rp(r);
			rp.ShutDown(true, false, true);
			break;
		}

		case kMsgAskHalt:
		{
			BRoster roster;
			BRoster::Private rosterPrivate(roster);
			rosterPrivate.ShutDown(false, true, true);
			break;
		}

		// ---- Dischi ----

		case kToggleVolume:
		{
			int32 partitionId;
			bool mount;
			if (msg->FindInt32("partition_id", &partitionId) == B_OK &&
				msg->FindBool("mount", &mount) == B_OK) {

				struct MountData {
					int32 partitionId;
					bool mount;
				};

				MountData* data = new MountData;
				data->partitionId = partitionId;
				data->mount = mount;

				thread_id thread = spawn_thread(_MountWorker, "mount_worker",
					B_NORMAL_PRIORITY, data);
				if (thread >= 0) {
					resume_thread(thread);

					BMessage refreshMsg(kMsgRefreshVolumes);
					BMessageRunner::StartSending(this, &refreshMsg,
						500000, 1);
				} else {
					delete data;
				}
			}
			break;
		}

		case kMsgRefreshVolumes:
			if (fState.world == kWorldDisks) {
				_RefreshVolumes();
				_Rebuild();
			}
			break;

		case kUnmountAll:
		{
			thread_id thread = spawn_thread(_UnmountAllWorker, "unmount_all",
				B_NORMAL_PRIORITY, this);
			if (thread >= 0)
				resume_thread(thread);
			break;
		}

		case kMountAllNow:
		{
			thread_id thread = spawn_thread(_MountAllWorker, "mount_all",
				B_NORMAL_PRIORITY, this);
			if (thread >= 0)
				resume_thread(thread);
			break;
		}

		case kMountVolume:
		{
			int32 id;
			if (msg->FindInt32("id", &id) == B_OK) {
				BDiskDeviceList devices;
				if (devices.Fetch() == B_OK) {
					BPartition* partition = devices.PartitionWithID(id);
					if (partition)
						partition->Mount();
				}
			}
			break;
		}

		case kUnmountVolume:
		{
			int32 id;
			if (msg->FindInt32("id", &id) == B_OK) {
				BDiskDeviceList devices;
				if (devices.Fetch() == B_OK) {
					BPartition* partition = devices.PartitionWithID(id);
					if (partition && partition->IsMounted())
						partition->Unmount();
				}
			}

			int32 deviceId;
			if (msg->FindInt32("device_id", &deviceId) == B_OK) {
				BVolume volume(deviceId);
				if (volume.InitCheck() == B_OK) {
					BMessenger tracker("application/x-vnd.Be-TRAK");
					BMessage unmountMsg('Tunm');

					BDirectory rootDir;
					volume.GetRootDirectory(&rootDir);
					BEntry entry;
					rootDir.GetEntry(&entry);
					entry_ref ref;
					entry.GetRef(&ref);

					unmountMsg.AddRef("refs", &ref);
					tracker.SendMessage(&unmountMsg);
				}
			}
			break;
		}

		case kRunAutomounterSettings:
		{
			BMessenger tracker("application/x-vnd.Be-TRAK");
			if (tracker.IsValid()) {
				BMessage cmd('Tram');
				tracker.SendMessage(&cmd);
			}
			break;
		}

		// ---- Query e cartelle ----

		case kMsgOpenQueryWindow:
		{
			BMessenger tracker("application/x-vnd.Be-TRAK", -1);
			if (tracker.IsValid()) {
				BMessage findMessage('Tfnd');
				tracker.SendMessage(&findMessage);
			}
			break;
		}

		case kOpenQuery:
		{
			entry_ref ref;
			if (msg->FindRef("refs", &ref) == B_OK) {
				be_roster->Launch(&ref);
			} else {
				BString path;
				if (msg->FindString("path", &path) == B_OK
					&& get_ref_for_path(path.String(), &ref) == B_OK)
					be_roster->Launch(&ref);
			}
			break;
		}

		case kMsgOpenFolder:
		{
			BString path;
			if (msg->FindString("path", &path) == B_OK) {
				BEntry entry(path.String());
				if (!entry.Exists())
					create_directory(path.String(), 0755);
				OpenFolderInTracker(path.String());
			}
			break;
		}

		// ---- Risultati di Cerca ----

		case kMsgSearchResults:
			_HandleSearchResults(msg);
			break;

		case kMsgSearchDone:
		{
			int32 gen;
			if (msg->FindInt32("gen", &gen) != B_OK
				|| (uint32)gen != fSearchGen->load())
				break;
			fSearchDone = true;
			msg->FindBool("truncated", &fSearchTruncated);
			if (fSearchCount == 0)
				fMainList->SetEmptyMessage(B_TRANSLATE("No matches"));
			_UpdateSearchCounter();
			break;
		}

		default:
			BWindow::MessageReceived(msg);
			break;
	}
}

void
DeskbarWindow::DispatchMessage(BMessage* message, BHandler* target)
{
	if (message->what == B_KEY_DOWN) {
		const char* bytes;
		int32 modifiers = 0;
		message->FindInt32("modifiers", &modifiers);

		if (message->FindString("bytes", &bytes) == B_OK
			&& (modifiers & B_COMMAND_KEY) != 0) {
			if (_HandleCommandKey(bytes[0], modifiers))
				return;
		} else if (message->FindString("bytes", &bytes) == B_OK
			&& fFilter->TextView()->IsFocus()) {
			if (_HandleListKey(message, bytes[0]))
				return;
		}
	}

	BWindow::DispatchMessage(message, target);
}

// ---- Stato -------------------------------------------------------
// Piano par. 5.2.
void
DeskbarWindow::_SetState(FacetState wanted, int32 source, bool force )
{
	// 1. Normalizzare.
	wanted.kindMask &= kKindFilterMask;
	if (wanted.kindMask == kKindFilterMask)
		wanted.kindMask = 0;	// tutti i tipi == nessuna restrizione
	wanted.tags &= kTagFilterMask;	// Recent+Favorite = OR, resta com'e'
	if (wanted.world < kWorldCatalog || wanted.world > kWorldSearch)
		wanted.world = kWorldCatalog;

	if (source == kFromUser && fSettings.startupMode == kStartLast) {
		ViewSpec spec;
		spec.world = wanted.world;
		spec.kindMask = wanted.kindMask;
		spec.tags = wanted.tags;
		fSettings.lastView = spec;
		SettingsSave(fSettings);
	}

	// Stato uguale: fermarsi.
	if (!force && wanted == fState) {
		_SyncControls();
		return;
	}

	World oldWorld = fState.world;
	fState = wanted;
	bool worldChanged = force || fState.world != oldWorld;

	if (worldChanged)
		_OnWorldChanged(oldWorld);

	_SyncControls();

	delete fDebounceRunner;
	fDebounceRunner = nullptr;
	fUserMovedSelection = false;
	_Rebuild();
}

void
DeskbarWindow::_OnWorldChanged(World oldWorld)
{
	if (oldWorld == kWorldSearch && fState.world != kWorldSearch)
		++(*fSearchGen);	// scarta i risultati in arrivo

	if (fState.world == kWorldDisks)
		_RefreshVolumes();

	if (fState.world == kWorldCatalog && oldWorld != kWorldCatalog) {
		_RefreshFavorites();
		_RefreshRecents();
		_RefreshFlags();
	}

	const char* placeholder;
	BString tip;
	switch (fState.world) {
		case kWorldSearch:
			placeholder = B_TRANSLATE("Search disk" B_UTF8_ELLIPSIS);
			tip = B_TRANSLATE("Type at least 3 characters to search the disk. "
				"Use the arrow keys to move through the list, Enter to "
				"launch, F10 to open the menu.");
			break;
		case kWorldDisks:
			placeholder = B_TRANSLATE("Filter disks" B_UTF8_ELLIPSIS);
			tip = B_TRANSLATE("Type to filter by name. "
				"Use the arrow keys to move through the list, Enter to "
				"launch, F10 to open the menu.");
			break;
		case kWorldGo:
			placeholder = B_TRANSLATE("Filter Go" B_UTF8_ELLIPSIS);
			tip = B_TRANSLATE("Type to filter by name. "
				"Use the arrow keys to move through the list, Enter to "
				"launch, F10 to open the menu.");
			break;
		default:
			placeholder = B_TRANSLATE("Filter catalog" B_UTF8_ELLIPSIS);
			tip = B_TRANSLATE("Type to filter by name. "
				"Use the arrow keys to move through the list, Enter to "
				"launch, F10 to open the menu.");
			break;
	}
	fFilter->SetPlaceholder(placeholder);
	fFilter->SetToolTip(tip.String());
}

// Allinea pulsanti e segmenti allo stato, senza messaggi di ritorno.
// I segmenti fuori dal loro mondo restano visibili ma disabilitati:
// la riga non cambia mai posto ne' altezza.
void
DeskbarWindow::_SyncControls()
{
	fDisksBtn->SetValue(fState.world == kWorldDisks ? B_CONTROL_ON : B_CONTROL_OFF);
	fGoBtn->SetValue(fState.world == kWorldGo ? B_CONTROL_ON : B_CONTROL_OFF);
	fQueryBtn->SetValue(fState.world == kWorldSearch ? B_CONTROL_ON : B_CONTROL_OFF);

	fKindSeg->SetActiveMask(fState.kindMask);
	fKindSeg->SetSegmentsEnabled(_HasFacets());	// Catalogo e Cerca
	fTagSeg->SetActiveMask(fState.tags);
	fTagSeg->SetSegmentsEnabled(fState.world == kWorldCatalog);

	// Il testo resta quando si cambia mondo.
	if (BString(fFilter->Text()) != fState.text)
		fFilter->SetText(fState.text.String());
}

void
DeskbarWindow::_UpdateCounter(int32 count, int mode)
{
	fCounter->SetText(FormatCount(count, mode).String());
}

void
DeskbarWindow::_ClearCounter()
{
	fCounter->SetText("");
}

// Raggruppamento in sezioni: solo nel Catalogo, con l'opzione attiva e
// senza restrizione di tag (o con entrambi i tag, che in OR lasciano
// solo le sezioni Preferiti e Recenti).
bool
DeskbarWindow::_IsGrouped() const
{
	return fState.world == kWorldCatalog && fSettings.options.groupByTag
		&& (fState.tags == 0 || fState.tags == kTagFilterMask);
}

// ---- Proprieta' degli item (piano par. 3.5) ------------------------
void
DeskbarWindow::_FreeTransient()
{
	for (BListItem* item : fTransient)
		delete item;
	fTransient.clear();
}

void
DeskbarWindow::_ClearList()
{
	fMainList->MakeEmpty();
	_FreeTransient();
	fMainList->ResetHover();
	fMainList->ScrollTo(BPoint(0, 0));
}

// ---- Ricostruzione della lista (piano par. 5.4) ----------------------
void
DeskbarWindow::_Rebuild()
{
	switch (fState.world) {
		case kWorldCatalog:
			_RebuildCatalog();
			break;
		case kWorldDisks:
			_RebuildDisks();
			break;
		case kWorldGo:
			_RebuildGo();
			break;
		case kWorldSearch:
			_StartSearch();
			break;
	}
}

void
DeskbarWindow::_AddSection(const char* label, std::vector<UnifiedListItem*>& items)
{
	if (items.empty())
		return;
	SectionHeaderItem* header = new SectionHeaderItem(label);
	fTransient.push_back(header);
	fMainList->AddItem(header);
	for (UnifiedListItem* item : items)
		fMainList->AddItem(item);
}

void
DeskbarWindow::_SelectFirst()
{
	int32 first = _NextSelectable(-1, 1);
	if (first >= 0) {
		fMainList->Select(first);
		fMainList->ScrollTo(BPoint(0, 0));
	}
}

// ---- Selezione e tastiera ---------------------------------------------
int32
DeskbarWindow::_NextSelectable(int32 from, int32 direction)
{
	int32 count = fMainList->CountItems();
	for (int32 i = from + direction; i >= 0 && i < count; i += direction) {
		if (dynamic_cast<SectionHeaderItem*>(fMainList->ItemAt(i)) == NULL)
			return i;
	}
	return -1;
}

void
DeskbarWindow::_SelectIndex(int32 index, bool byUser)
{
	fMainList->Select(index);
	if (index == _NextSelectable(-1, 1))
		fMainList->ScrollTo(BPoint(0, 0));	// rivela l'intestazione sopra
	else
		fMainList->ScrollToSelection();
	if (byUser)
		fUserMovedSelection = true;
}

UnifiedListItem*
DeskbarWindow::_SelectedItem()
{
	int32 index = fMainList->CurrentSelection();
	if (index < 0)
		return NULL;
	return dynamic_cast<UnifiedListItem*>(fMainList->ItemAt(index));
}
