#include "ManualChecksumWindow.h"
#include "HPKGIntegrityConstants.h"

#include <Alert.h>
#include <Button.h>
#include <File.h>
#include <GroupLayout.h>
#include <GroupView.h>
#include <SpaceLayoutItem.h>
#include <StringView.h>
#include <TextControl.h>

#include <AutoLocker.h>

#include <string.h>

#include <crypto/BCrypto.h>

using namespace BPackageKit::BPrivate;

#define MSG_CALC_CHECKSUM 'ClCs'
#define MSG_SAVE_CHECKSUM 'SvCs'
#define MSG_CANCEL_CHECKSUM 'CnCs'


ManualChecksumWindow::ManualChecksumWindow(const char* packageName,
	const char* packagePath)
	:
	BWindow(BRect(150, 150, 650, 300), "Set Checksum", B_TITLED_WINDOW_LOOK,
		B_FLOATING_ALL_WINDOW_FEEL,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
			| B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_CLOSE_ON_ESCAPE,
		B_ALL_WORKSPACES),
	fPackagePath(packagePath),
	fDoneSemaphore(-1),
	fClientWaiting(false),
	fAccepted(false),
	fChecksumControl(NULL),
	fCalculateButton(NULL),
	fCancelButton(NULL),
	fSaveButton(NULL)
{
	fDoneSemaphore = create_sem(0, "manual package checksum");

	BString line1;
	line1.SetToFormat("No integrity.hpkg.sha256 on file for '%s' - enter a "
		"checksum you trust (e.g. from the vendor's site),", packageName);
	BStringView* infoView1 = new BStringView(NULL, line1.String());
	BStringView* infoView2 = new BStringView(NULL,
		"or calculate and review the file's current hash before saving it.");

	fChecksumControl = new BTextControl("checksum", "SHA256:", "", NULL);

	fCalculateButton = new BButton("calc_btn", "Calculate Checksum",
		new BMessage(MSG_CALC_CHECKSUM));
	fCalculateButton->SetTarget(this);

	fCancelButton = new BButton("cancel_btn", "Cancel",
		new BMessage(MSG_CANCEL_CHECKSUM));
	fCancelButton->SetTarget(this);

	fSaveButton = new BButton("save_btn", "Save",
		new BMessage(MSG_SAVE_CHECKSUM));
	fSaveButton->SetTarget(this);

	BGroupView* root = new BGroupView(B_VERTICAL, 10);
	SetLayout(root->GroupLayout());
	root->GroupLayout()->SetInsets(10, 10, 10, 10);

	root->GroupLayout()->AddView(infoView1, 0.0f);
	root->GroupLayout()->AddView(infoView2, 0.0f);
	root->GroupLayout()->AddView(fChecksumControl, 0.0f);

	BGroupView* buttonRow = new BGroupView(B_HORIZONTAL, 5);
	buttonRow->GroupLayout()->AddView(fCalculateButton);
	buttonRow->GroupLayout()->AddItem(BSpaceLayoutItem::CreateGlue());
	buttonRow->GroupLayout()->AddView(fCancelButton);
	buttonRow->GroupLayout()->AddView(fSaveButton);
	root->GroupLayout()->AddView(buttonRow, 0.0f);

	BView* topView = ChildAt(0);
	if (topView != NULL)
		topView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
}


ManualChecksumWindow::~ManualChecksumWindow()
{
	if (fDoneSemaphore >= 0)
		delete_sem(fDoneSemaphore);
}


bool
ManualChecksumWindow::Go(BString& _checksum)
{
	AutoLocker<ManualChecksumWindow> locker(this);

	CenterOnScreen();
	Show();

	fAccepted = false;
	fClientWaiting = true;

	locker.Unlock();

	while (acquire_sem(fDoneSemaphore) == B_INTERRUPTED) {
	}

	locker.Lock();
	bool result = false;
	if (locker.IsLocked()) {
		result = fAccepted;
		if (result)
			_checksum = fChecksumResult;
		Quit();
		locker.Detach();
	} else
		PostMessage(B_QUIT_REQUESTED);

	return result;
}


bool
ManualChecksumWindow::QuitRequested()
{
	if (fClientWaiting) {
		Hide();
		fClientWaiting = false;
		release_sem(fDoneSemaphore);
		return false;
	}

	return true;
}


void
ManualChecksumWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_CALC_CHECKSUM:
			_CalculateChecksum();
			break;

		case MSG_SAVE_CHECKSUM:
			_Save();
			break;

		case MSG_CANCEL_CHECKSUM:
			if (fClientWaiting) {
				Hide();
				fAccepted = false;
				fClientWaiting = false;
				release_sem(fDoneSemaphore);
			}
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
ManualChecksumWindow::_CalculateChecksum()
{
	BFile file(fPackagePath.String(), B_READ_ONLY);
	status_t status = file.InitCheck();
	if (status != B_OK) {
		BString message;
		message.SetToFormat("Could not open the file: %s", strerror(status));
		(new BAlert("Calculate Checksum", message.String(), "OK"))->Go();
		return;
	}

	BCrypto crypto;
	uint8 digest[32];
	status = crypto.Digest(B_CRYPTO_SHA256, &file, digest);
	if (status != B_OK) {
		BString message;
		message.SetToFormat("Failed to calculate checksum: %s",
			strerror(status));
		(new BAlert("Calculate Checksum", message.String(), "OK"))->Go();
		return;
	}

	char hex[HPKG_CHECKSUM_BUFFER_SIZE];
	_HPKGDigestToHex(digest, sizeof(digest), hex);
	fChecksumControl->SetText(hex);
}


void
ManualChecksumWindow::_Save()
{
	BString checksum = fChecksumControl->Text();
	checksum.ToLower();

	if (!_HPKGIsValidChecksumHex(checksum.String())) {
		(new BAlert("Save Checksum",
			"That doesn't look like a SHA256 checksum - it needs to be "
			"exactly 64 hex characters (0-9, a-f). Use \"Calculate "
			"Checksum\" if you just want the file's current hash.",
			"OK"))->Go();
		return;
	}

	if (fClientWaiting) {
		fChecksumResult = checksum;
		Hide();
		fAccepted = true;
		fClientWaiting = false;
		release_sem(fDoneSemaphore);
	}
}
