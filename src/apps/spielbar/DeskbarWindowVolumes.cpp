/*
 * SpielBar v3 - la finestra principale
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#include <Application.h>
#include <Bitmap.h>
#include <Catalog.h>
#include <Directory.h>
#include <DiskDevice.h>
#include <DiskDeviceList.h>
#include <Entry.h>
#include <List.h>
#include <ListItem.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <Partition.h>
#include <Path.h>
#include <Point.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <StorageDefs.h>
#include <String.h>
#include <SupportDefs.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include "DeskbarWindow.h"
#include "SpielBarDefs.h"
#include "UiMetrics.h"
#include "IconSupport.h"
#include "Emblems.h"
#include "VolumeIcons.h"
#include "ListItems.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


// ---- Volumi ---------------------------------------------------------
void
DeskbarWindow::_RefreshVolumes()
{
	// Gli item attuali potrebbero essere nella lista: svuotarla prima.
	fMainList->MakeEmpty();
	fMainList->ResetHover();
	for (BListItem* item : fVolumesCache)
		delete item;
	fVolumesCache.clear();

	BList temp;
	_LoadVolumes(&temp);
	for (int32 i = 0; i < temp.CountItems(); i++)
		fVolumesCache.push_back((BListItem*)temp.ItemAt(i));
}

void
DeskbarWindow::_LoadVolumes(BList* outList)
{
	if (!outList)
		return;

	BDiskDeviceList devices;
	if (devices.Fetch() != B_OK)
		return;

	class VolumeVisitor : public BDiskDeviceVisitor {
	public:
		VolumeVisitor(BList* list) : fList(list) {}

		virtual bool Visit(BDiskDevice* device)
		{
			return Visit(device, 0);
		}

		virtual bool Visit(BPartition* partition, int32 level)
		{
			if (!partition->ContainsFileSystem())
				return false;

			BString name = partition->ContentName();
			if (name.Length() == 0) {
				name = partition->Name();
				if (name.Length() == 0) {
					const char* type = partition->ContentType();
					if (type == NULL)
						return false;

					uint32 divisor = 1UL << 30;
					char unit = 'G';
					if (partition->Size() < divisor) {
						divisor = 1UL << 20;
						unit = 'M';
					}

					name.SetToFormat("%.1f %cB %s",
						1.0 * partition->Size() / divisor, unit, type);
				}
			}

			BString info;
			int32 percentUsed = -1;

			if (partition->IsMounted()) {
				BVolume volume;
				if (partition->GetVolume(&volume) == B_OK) {
					BDirectory rootDir;
					volume.GetRootDirectory(&rootDir);
					BPath path(&rootDir, ".");
					info = path.Path();

					off_t capacity = volume.Capacity();
					off_t freeBytes = volume.FreeBytes();
					if (capacity > 0) {
						percentUsed = (int32)((100.0 * (capacity - freeBytes))
							/ (double)capacity + 0.5);
					}
				}
			} else {
				info = B_TRANSLATE("Not mounted");
			}

			// VolumeIcons risolve l'icona nell'ordine che usa Tracker:
			// BEOS:ICON del volume -> icona del driver (HVIF) -> icona
			// MIME generica. Vale anche per le partizioni non montate.
			BBitmap* icon = volumeicons::ForPartition(partition,
				(float)UI().rowIcon);
			if (icon == NULL)
				icon = LoadVectorIcon(kIconMount, UI().rowIcon);

			VolumeListItem* item = new VolumeListItem(
				name.String(),
				info.String(),
				partition->ID(),
				partition->IsMounted(),
				percentUsed,
				icon
			);

			fList->AddItem(item);
			return false;
		}

	private:
		BList* fList;
	} visitor(outList);

	devices.VisitEachPartition(&visitor);

	BVolumeRoster volumeRoster;
	BVolume volume;
	while (volumeRoster.GetNextVolume(&volume) == B_OK) {
		if (volume.IsShared()) {
			char volumeName[B_FILE_NAME_LENGTH];
			volume.GetName(volumeName);

			BDirectory rootDir;
			volume.GetRootDirectory(&rootDir);
			BPath path(&rootDir, ".");

			int32 percentUsed = -1;
			off_t capacity = volume.Capacity();
			off_t freeBytes = volume.FreeBytes();
			if (capacity > 0) {
				percentUsed = (int32)((100.0 * (capacity - freeBytes))
					/ (double)capacity + 0.5);
			}

			BBitmap* icon = volumeicons::ForVolume(volume,
				(float)UI().rowIcon);
			if (icon == NULL)
				icon = volumeicons::GenericVolume((float)UI().rowIcon);
			if (icon == NULL)
				icon = LoadVectorIcon(kIconMount, UI().rowIcon);

			VolumeListItem* item = new VolumeListItem(
				volumeName,
				path.Path(),
				-volume.Device(),
				true,
				percentUsed,
				icon
			);

			outList->AddItem(item);
		}
	}
}

void
DeskbarWindow::_RebuildDisks()
{
	_ClearList();
	fMainList->SetStarsEnabled(false);

	int32 count = 0;
	for (BListItem* base : fVolumesCache) {
		UnifiedListItem* item = dynamic_cast<UnifiedListItem*>(base);
		if (item == NULL)
			continue;
		if (fState.text.IsEmpty()
			|| item->Name().IFindFirst(fState.text) >= 0
			|| item->Path().IFindFirst(fState.text) >= 0) {
			fMainList->AddItem(item);
			count++;
		}
	}

	fMainList->SetEmptyMessage(fState.text.IsEmpty()
		? B_TRANSLATE("<empty>") : B_TRANSLATE("No matches"));
	_UpdateCounter(count, 0);
	_SelectFirst();
}

void
DeskbarWindow::_ShowVolumeMenu(BPoint where, VolumeListItem* volItem)
{
	BPopUpMenu* menu = new BPopUpMenu("volume_context", false, false);

	if (volItem->IsMounted()) {
		bool isBoot = false;

		if (volItem->PartitionId() > 0) {
			BDiskDeviceList devices;
			if (devices.Fetch() == B_OK) {
				BPartition* partition
					= devices.PartitionWithID(volItem->PartitionId());
				if (partition) {
					BVolume volume;
					if (partition->GetVolume(&volume) == B_OK) {
						BVolume bootVolume;
						BVolumeRoster().GetBootVolume(&bootVolume);
						if (volume == bootVolume)
							isBoot = true;
					}
				}
			}
		}

		if (!isBoot) {
			menu->AddItem(new BMenuItem(B_TRANSLATE("Unmount"),
				new BMessage(kToggleVolume)));
		}

		if (volItem->PartitionId() > 0) {
			BDiskDeviceList devices;
			if (devices.Fetch() == B_OK) {
				BPartition* partition
					= devices.PartitionWithID(volItem->PartitionId());
				if (partition) {
					BVolume volume;
					if (partition->GetVolume(&volume) == B_OK) {
						BDirectory rootDir;
						volume.GetRootDirectory(&rootDir);
						BEntry entry;
						rootDir.GetEntry(&entry);
						entry_ref ref;
						if (entry.GetRef(&ref) == B_OK) {
							BMessage* showMsg = new BMessage(kMsgRevealInTracker);
							showMsg->AddRef("refs", &ref);
							menu->AddItem(new BMenuItem(
								B_TRANSLATE("Reveal in Tracker"), showMsg, 'R'));

							BMessage* infoMsg = new BMessage(kMsgGetInfo);
							infoMsg->AddRef("refs", &ref);
							menu->AddItem(new BMenuItem(B_TRANSLATE("Get Info"),
								infoMsg, 'I'));
						}
					}
				}
			}
		}
	} else {
		menu->AddItem(new BMenuItem(B_TRANSLATE("Mount"),
			new BMessage(kToggleVolume)));
	}

	for (int32 i = 0; i < menu->CountItems(); i++) {
		BMenuItem* item = menu->ItemAt(i);
		if (item->Message() && item->Message()->what == kToggleVolume) {
			item->Message()->AddInt32("partition_id", volItem->PartitionId());
			item->Message()->AddBool("mount", !volItem->IsMounted());
		}
	}

	SetMenuTarget(menu, this);
	menu->Go(fMainList->ConvertToScreen(where), true, false, true);
}

// ---- Worker di mount/unmount (invariati, salvo il messaggio di refresh) --
int32
DeskbarWindow::_MountAllWorker(void* data)
{
	DeskbarWindow* win = reinterpret_cast<DeskbarWindow*>(data);

	BDiskDeviceList devices;
	if (devices.Fetch() == B_OK) {
		class MountAllVisitor : public BDiskDeviceVisitor {
		public:
			using BDiskDeviceVisitor::Visit;
			virtual bool Visit(BPartition* partition, int32)
			{
				if (partition->ContainsFileSystem() && !partition->IsMounted())
					partition->Mount();
				return false;
			}
		} visitor;
		devices.VisitEachPartition(&visitor);
	}

	BMessenger(win).SendMessage(kMsgRefreshVolumes);
	return 0;
}

int32
DeskbarWindow::_MountWorker(void* data)
{
	struct MountData {
		int32 partitionId;
		bool mount;
	};

	MountData* mountData = (MountData*)data;

	BDiskDeviceList devices;
	if (devices.Fetch() == B_OK) {
		BPartition* partition = devices.PartitionWithID(mountData->partitionId);
		if (partition) {
			if (mountData->mount)
				partition->Mount();
			else
				partition->Unmount();
		}
	}

	delete mountData;
	return 0;
}

int32
DeskbarWindow::_UnmountAllWorker(void* data)
{
	DeskbarWindow* win = reinterpret_cast<DeskbarWindow*>(data);

	BDiskDeviceList devices;
	if (devices.Fetch() == B_OK) {
		class UnmountAllVisitor : public BDiskDeviceVisitor {
		public:
			using BDiskDeviceVisitor::Visit;
			virtual bool Visit(BPartition* partition, int32)
			{
				if (partition->ContainsFileSystem() && partition->IsMounted()) {
					BVolume volume;
					if (partition->GetVolume(&volume) == B_OK) {
						BVolume bootVolume;
						BVolumeRoster().GetBootVolume(&bootVolume);
						if (volume != bootVolume)
							partition->Unmount();
					}
				}
				return false;
			}
		} visitor;
		devices.VisitEachPartition(&visitor);
	}

	BVolumeRoster volumeRoster;
	BVolume volume;
	while (volumeRoster.GetNextVolume(&volume) == B_OK) {
		if (volume.IsShared()) {
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

	if (win)
		BMessenger(win).SendMessage(kMsgRefreshVolumes);

	return 0;
}
