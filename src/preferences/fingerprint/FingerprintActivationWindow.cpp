/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "FingerprintActivationWindow.h"

#include <stdio.h>

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Message.h>
#include <Messenger.h>
#include <Roster.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <StringView.h>

#include "FingerListItem.h"
#include "../../servers/fingerprint/defs.h"

// Not including headers/private/app/KeyStoreDefs.h here: it defines
// BPrivate::kKeyStoreServerSignature as a non-inline `const char*` global,
// and FingerprintWindow.cpp (linked into the same app) already defines it
// too - a second definition in this translation unit is a multiple-
// definition link error. KEY_STORE_LIST_REMOVABLE_VOLUMES's value is
// duplicated locally instead (canonical definition: KeyStoreDefs.h).


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "FingerprintActivationWindow"


enum {
	kMsgAddToSequence	= 'faAd',
	kMsgResetSequence	= 'faRs',
	kMsgVolumeSelected	= 'faVs',
	kMsgCancel			= 'faCl',
	kMsgConfirm			= 'faCf',

	kKeyStoreListRemovableVolumes = 'KfpV',
};


class VolumeListItem : public BStringItem {
public:
	VolumeListItem(const char* name, dev_t device)
		:
		BStringItem(name),
		fDevice(device)
	{
	}

	dev_t Device() const { return fDevice; }

private:
	dev_t fDevice;
};


static BMessenger
FingerprintServerMessenger()
{
	if (!be_roster->IsRunning(FINGERPRINT_SERVER_SIGNATURE))
		be_roster->Launch(FINGERPRINT_SERVER_SIGNATURE);
	return BMessenger(FINGERPRINT_SERVER_SIGNATURE);
}


FingerprintActivationWindow::FingerprintActivationWindow()
	:
	BWindow(BRect(120, 120, 520, 480),
		B_TRANSLATE("Fingerprint unlock setup"),
		B_TITLED_WINDOW_LOOK, B_MODAL_ALL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE | B_ASYNCHRONOUS_CONTROLS
			| B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fSequenceLength(0),
	fDoneSem(-1),
	fResult(B_CANCELED)
{
	fDoneSem = create_sem(0, "fingerprint activation dialog");

	fVolumeList = new BListView("volumeList");
	fVolumeList->SetSelectionMessage(new BMessage(kMsgVolumeSelected));
	BScrollView* volumeScroll = new BScrollView("volumeScroll", fVolumeList,
		0, false, false);

	fFingerList = new BListView("fingerList");
	BScrollView* fingerScroll = new BScrollView("fingerScroll", fFingerList,
		0, false, false);

	fSequenceStatus = new BStringView("sequenceStatus", "");

	fAddButton = new BButton("add", B_TRANSLATE("Add to sequence"),
		new BMessage(kMsgAddToSequence));
	fResetButton = new BButton("reset", B_TRANSLATE("Reset sequence"),
		new BMessage(kMsgResetSequence));
	fCancelButton = new BButton("cancel", B_TRANSLATE("Cancel"),
		new BMessage(kMsgCancel));
	fConfirmButton = new BButton("confirm", B_TRANSLATE("Activate"),
		new BMessage(kMsgConfirm));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(new BStringView("volumeLabel",
			B_TRANSLATE("USB stick to dedicate to unlocking:")))
		.Add(volumeScroll)
		.Add(new BStringView("fingerLabel",
			B_TRANSLATE("Select a finger and add it to the sequence, 4 "
				"times (the same finger can repeat):")))
		.Add(fingerScroll)
		.AddGroup(B_HORIZONTAL)
			.Add(fAddButton)
			.Add(fResetButton)
		.End()
		.Add(fSequenceStatus)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fCancelButton)
			.Add(fConfirmButton)
		.End()
	.End();

	_RefreshVolumes();
	_RefreshFingers();
	_UpdateStatus();
	_UpdateConfirmEnabled();
}


FingerprintActivationWindow::~FingerprintActivationWindow()
{
	if (fDoneSem >= 0)
		delete_sem(fDoneSem);
}


void
FingerprintActivationWindow::_RefreshVolumes()
{
	while (fVolumeList->CountItems() > 0)
		delete fVolumeList->RemoveItem((int32)0);

	// Avoid including BPrivate::kKeyStoreServerSignature here: it's a
	// non-inline `const char*` global in KeyStoreDefs.h, so a second
	// translation unit in the same binary defining it too (this file,
	// alongside FingerprintWindow.cpp which already uses it) is a
	// multiple-definition link error. Same string value, as a local
	// literal instead.
	BMessenger keyStore("application/x-vnd.Haiku-keystore_server");
	if (!keyStore.IsValid())
		return;

	BMessage request(kKeyStoreListRemovableVolumes);
	BMessage reply;
	if (keyStore.SendMessage(&request, &reply) != B_OK)
		return;

	BString name;
	int32 device;
	for (int32 i = 0; reply.FindString("name", i, &name) == B_OK; i++) {
		reply.FindInt32("device", i, &device);
		fVolumeList->AddItem(new VolumeListItem(name.String(),
			(dev_t)device));
	}
}


void
FingerprintActivationWindow::_RefreshFingers()
{
	while (fFingerList->CountItems() > 0)
		delete fFingerList->RemoveItem((int32)0);

	BMessage request(B_FP_LIST);
	BMessage reply;
	if (FingerprintServerMessenger().SendMessage(&request, &reply) != B_OK)
		return;

	int32 fingerId;
	BString label;
	int64 createdAt;
	for (int32 i = 0; reply.FindInt32("fingerId", i, &fingerId) == B_OK; i++) {
		reply.FindString("label", i, &label);
		reply.FindInt64("createdAt", i, &createdAt);
		fFingerList->AddItem(new FingerListItem(fingerId, label.String(),
			createdAt));
	}
}


void
FingerprintActivationWindow::_AddSelectedToSequence()
{
	if (fSequenceLength >= 4)
		return;

	int32 selected = fFingerList->CurrentSelection();
	if (selected < 0)
		return;

	FingerListItem* item = (FingerListItem*)fFingerList->ItemAt(selected);
	fSequence[fSequenceLength++] = item->FingerId();

	_UpdateStatus();
	_UpdateConfirmEnabled();
}


void
FingerprintActivationWindow::_ResetSequence()
{
	fSequenceLength = 0;
	_UpdateStatus();
	_UpdateConfirmEnabled();
}


void
FingerprintActivationWindow::_UpdateStatus()
{
	BString status;
	status << B_TRANSLATE("Sequence") << " (" << fSequenceLength << "/4): ";
	for (int32 i = 0; i < fSequenceLength; i++) {
		if (i > 0)
			status << ", ";
		status << "#" << fSequence[i];
	}
	fSequenceStatus->SetText(status.String());
}


void
FingerprintActivationWindow::_UpdateConfirmEnabled()
{
	bool haveVolume = fVolumeList->CurrentSelection() >= 0;
	fConfirmButton->SetEnabled(haveVolume && fSequenceLength == 4);
	fAddButton->SetEnabled(fSequenceLength < 4);
}


void
FingerprintActivationWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgAddToSequence:
			_AddSelectedToSequence();
			break;

		case kMsgResetSequence:
			_ResetSequence();
			break;

		case kMsgVolumeSelected:
			_UpdateConfirmEnabled();
			break;

		case kMsgCancel:
			fResult = B_CANCELED;
			release_sem(fDoneSem);
			break;

		case kMsgConfirm:
			fResult = B_OK;
			release_sem(fDoneSem);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
FingerprintActivationWindow::QuitRequested()
{
	fResult = B_CANCELED;
	release_sem(fDoneSem);
	return false;
}


status_t
FingerprintActivationWindow::RequestActivation(int32* outSequence,
	dev_t* outDevice)
{
	CenterOnScreen();
	Show();

	while (acquire_sem(fDoneSem) == B_INTERRUPTED)
		;

	status_t result = fResult;
	if (result == B_OK) {
		for (int32 i = 0; i < 4; i++)
			outSequence[i] = fSequence[i];

		int32 selected = fVolumeList->CurrentSelection();
		VolumeListItem* item = (VolumeListItem*)fVolumeList->ItemAt(selected);
		*outDevice = item->Device();
	}

	LockLooper();
	Quit();
	return result;
}
