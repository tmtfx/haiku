/*
 * SpielBar v3 - apertura di cartelle e "Get info" tramite Tracker
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#include <AppDefs.h>
#include <Entry.h>
#include <Message.h>
#include <Messenger.h>
#include <Node.h>
#include <TypeConstants.h>

#include "TrackerBridge.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


void
OpenInfo(const char* path)
{
	BEntry entry(path, true);
	if (entry.InitCheck() != B_OK)
		return;

	entry_ref ref;
	if (entry.GetRef(&ref) != B_OK)
		return;

	BMessage message('Tinf');
	message.AddRef("refs", &ref);

	BMessenger tracker("application/x-vnd.Be-TRAK");
	if (tracker.IsValid())
		tracker.SendMessage(&message);
}

void
RevealInTracker(const entry_ref& ref)
{
	BEntry entry(&ref, true);
	if (!entry.Exists())
		return;

	node_ref node;
	if (entry.GetNodeRef(&node) != B_OK)
		return;

	BEntry parent;
	if (entry.GetParent(&parent) != B_OK)
		return;

	entry_ref parentRef;
	if (parent.GetRef(&parentRef) != B_OK)
		return;

	BMessenger trackerMessenger("application/x-vnd.Be-TRAK");
	if (trackerMessenger.IsValid()) {
		BMessage message(B_REFS_RECEIVED);
		message.AddRef("refs", &parentRef);
		message.AddData("nodeRefToSelect", B_RAW_TYPE, &node, sizeof(node_ref));
		trackerMessenger.SendMessage(&message);
	}
}

void
OpenFolderInTracker(const char* path)
{
	entry_ref ref;
	if (get_ref_for_path(path, &ref) != B_OK)
		return;
	BMessenger tracker("application/x-vnd.Be-TRAK");
	if (!tracker.IsValid())
		return;
	BMessage openMsg(B_REFS_RECEIVED);
	openMsg.AddRef("refs", &ref);
	tracker.SendMessage(&openMsg);
}
