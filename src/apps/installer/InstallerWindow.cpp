/*
 * Copyright 2026, I Pirati Del Frico
 * Copyright 2020-2023, Panagiotis "Ivory" Vasilopoulos <git@n0toose.net>
 * Copyright 2009-2010, Stephan Aßmus <superstippi@gmx.de>
 * Copyright 2005-2008, Jérôme DUVAL
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "InstallerWindow.h"

#include <stdio.h>
#include <strings.h>

#include <Alert.h>
#include <Application.h>
#include <Autolock.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <ColorConversion.h>
#include <ControlLook.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <File.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <LayoutUtils.h>
#include <Locale.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Screen.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <SpaceLayoutItem.h>
#include <StatusBar.h>
#include <String.h>
#include <TextView.h>
#include <TranslationUtils.h>
#include <TranslatorFormats.h>

#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/rsa.h>
#include <openssl/err.h>
#include <openssl/rand.h>

#include <DiskDevice.h>
#include <DiskDeviceRoster.h>
#include <Partition.h>

#include <new>

#include "crypto/BCrypto.h"

#include "tracker_private.h"

#include "DialogPane.h"
#include "InstallerDefs.h"
#include "PackageViews.h"
#include "PartitionMenuItem.h"
#include "WorkerThread.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "InstallerWindow"


static const char* kDriveSetupSignature = "application/x-vnd.Haiku-DriveSetup";
static const char* kBootManagerSignature = "application/x-vnd.Haiku-BootManager";

const uint32 BEGIN_MESSAGE = 'iBGN';
const uint32 SHOW_BOTTOM_MESSAGE = 'iSBT';
const uint32 LAUNCH_DRIVE_SETUP = 'iSEP';
const uint32 LAUNCH_BOOTMAN = 'iWBM';
const uint32 START_SCAN = 'iSSC';
const uint32 PACKAGE_CHECKBOX = 'iPCB';
const uint32 ENCOURAGE_DRIVESETUP = 'iENC';
const uint32 PASSWORD_UPDATED       = 'PSWU';
const uint32 MASTER_PASSWORD_SHOW   = 'MPSH';
const uint32 MASTER_PASSWORD_SAVE   = 'MPSV';


class LogoView : public BView {
public:
								LogoView(const BRect& frame);
								LogoView();
	virtual						~LogoView();

	virtual	void				Draw(BRect update);

	virtual	void				GetPreferredSize(float* _width,
									float* _height);

private:
			void				_Init();

			BBitmap*			fLogo;
};


LogoView::LogoView(const BRect& frame)
	:
	BView(frame, "logoview", B_FOLLOW_LEFT | B_FOLLOW_TOP,
		B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
{
	_Init();
}


LogoView::LogoView()
	:
	BView("logoview", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
{
	_Init();
}


LogoView::~LogoView(void)
{
	delete fLogo;
}


void
LogoView::Draw(BRect update)
{
	BRect bounds(Bounds());
	SetLowColor(ui_color(B_DOCUMENT_BACKGROUND_COLOR));
	FillRect(bounds, B_SOLID_LOW);

	if (fLogo == NULL)
		return;

	BPoint placement;
	placement.x = (bounds.left + bounds.right - fLogo->Bounds().Width()) / 2;
	placement.y = (bounds.top + bounds.bottom - fLogo->Bounds().Height()) / 2;

	DrawBitmap(fLogo, placement);
}


void
LogoView::GetPreferredSize(float* _width, float* _height)
{
	float width = 0.0;
	float height = 0.0;
	if (fLogo) {
		width = fLogo->Bounds().Width();
		height = fLogo->Bounds().Height();
	}
	if (_width)
		*_width = width;
	if (_height)
		*_height = height;
}


void
LogoView::_Init()
{
	SetDrawingMode(B_OP_OVER);

#ifdef HAIKU_DISTRO_COMPATIBILITY_OFFICIAL
	rgb_color bgColor = ui_color(B_DOCUMENT_BACKGROUND_COLOR);

	if (bgColor.IsLight())
		fLogo = BTranslationUtils::GetBitmap(B_PNG_FORMAT, "logo.png");
	else
		fLogo = BTranslationUtils::GetBitmap(B_PNG_FORMAT, "logo_dark.png");
#else
	fLogo = BTranslationUtils::GetBitmap(B_PNG_FORMAT, "walter_logo.png");
#endif
}



PasswordTC::PasswordTC(const char* label, BMessage* modificationMessage)
	:
	BTextControl(label, "", modificationMessage)//, fVisible(false)
{
}



// Test Decommentare se non funziona bene
/*
bool
PasswordTC::Visible() const
{
	return fVisible;
}


void
PasswordTC::SetVisible(bool visible)
{
	fVisible = visible;
	Invalidate();
}
*/
//void
//PasswordTC::DrawAfterChildren(BRect /*updateRect*/)
/*
{
	if (fVisible)
		return;

	BTextView* tv = TextView();
	BRect tvFrame = tv->Frame();

	// Cover the BTextView content area with its own background colour,
	// hiding whatever the BTextView just drew (real characters).
	SetHighColor(tv->ViewColor());
	FillRect(tvFrame);

	int32 len = tv->TextLength();
	if (len == 0)
		return;

	// Build the masked display string.
	BString masked;
	masked.Append('*', len);

	// Use the BTextView's font and text colour so the '*' glyphs look
	// identical to what the real text would have looked like.
	BFont font;
	rgb_color textColor;
	tv->GetFontAndColor(0, &font, &textColor);

	font_height fh;
	font.GetHeight(&fh);

	// PointAt(0) returns the upper-left corner of the first character
	// in BTextView coordinates; add ascent to land on the baseline.
	BPoint origin = tv->PointAt(0);
	BPoint drawPoint(
		tvFrame.left + origin.x,
		tvFrame.top  + origin.y + fh.ascent
	);

	SetHighColor(textColor);
	SetFont(&font);
	DrawString(masked.String(), drawPoint);
}*/

// #pragma mark -


static BLayoutItem*
layout_item_for(BView* view)
{
	BLayout* layout = view->Parent()->GetLayout();
	int32 index = layout->IndexOfView(view);
	return layout->ItemAt(index);
}

InstallerWindow::InstallerWindow()
    :
    BWindow(BRect(-2400, -2000, -1800, -1800),
        B_TRANSLATE_SYSTEM_NAME("Installer"), B_TITLED_WINDOW,
        B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
    fEncouragedToSetupPartitions(false),
    fDriveSetupLaunched(false),
    fBootManagerLaunched(false),
    fInstallStatus(kReadyForInstall),
    fWorkerThread(new WorkerThread(this)),
    fCardLayout(NULL),
    fCopyEngineCancelSemaphore(-1)
{
    if (!be_roster->IsRunning(kTrackerSignature))
        SetWorkspaces(B_ALL_WORKSPACES);

    LogoView* logoView = new LogoView();

    rgb_color baseColor = ui_color(B_DOCUMENT_TEXT_COLOR);
    fStatusView = new BTextView("statusView", be_plain_font, &baseColor,
        B_WILL_DRAW);
    fStatusView->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
    fStatusView->MakeEditable(false);
    fStatusView->MakeSelectable(false);

    BSize logoSize = logoView->MinSize();
    logoView->SetExplicitMaxSize(logoSize);

    // In the status view, make sure that we can display 5 lines of text of ~28 characters each
    font_height height;
    fStatusView->GetFontHeight(&height);
    float fontHeight = height.ascent + height.descent + height.leading;
    fStatusView->SetExplicitMinSize(BSize(fStatusView->StringWidth("W") * 28,
        fontHeight * 5 + 8));

    // Create a group view with a white background since the logo and status text won't have the
    // same height, this background will show in the remaining space
    fLogoGroup = new BGroupView(B_HORIZONTAL, 10);
    fLogoGroup->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
    fLogoGroup->GroupLayout()->SetInsets(0, 0, 10, 0);
    fLogoGroup->AddChild(logoView);
    fLogoGroup->AddChild(fStatusView);

    fDestMenu = new BPopUpMenu(B_TRANSLATE("scanning" B_UTF8_ELLIPSIS),
        true, false);
    fSrcMenu = new BPopUpMenu(B_TRANSLATE("scanning" B_UTF8_ELLIPSIS),
        true, false);

    fSrcMenuField = new BMenuField("srcMenuField",
        B_TRANSLATE("Install from:"), fSrcMenu);
    fSrcMenuField->SetAlignment(B_ALIGN_RIGHT);

    fDestMenuField = new BMenuField("destMenuField", B_TRANSLATE("Onto:"),
        fDestMenu);
    fDestMenuField->SetAlignment(B_ALIGN_RIGHT);

    fPackagesSwitch = new PaneSwitch("options_button");
    fPackagesSwitch->SetLabels(B_TRANSLATE("Hide optional packages"),
        B_TRANSLATE("Show optional packages"));
    fPackagesSwitch->SetMessage(new BMessage(SHOW_BOTTOM_MESSAGE));
    fPackagesSwitch->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED,
        B_SIZE_UNSET));
    fPackagesSwitch->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
        B_ALIGN_TOP));

    fPackagesView = new PackagesView("packages_view");
    BScrollView* packagesScrollView = new BScrollView("packagesScroll",
        fPackagesView, B_WILL_DRAW, false, true);

    const char* requiredDiskSpaceString
        = B_TRANSLATE("Additional disk space required: 0.0 KiB");
    fSizeView = new BStringView("size_view", requiredDiskSpaceString);
    fSizeView->SetAlignment(B_ALIGN_RIGHT);
    fSizeView->SetExplicitAlignment(
        BAlignment(B_ALIGN_RIGHT, B_ALIGN_TOP));

    fProgressBar = new BStatusBar("progress",
        B_TRANSLATE("Install progress:  "));
    fProgressBar->SetMaxValue(100.0);

    fBeginButton = new BButton("begin_button", B_TRANSLATE("Begin"),
        new BMessage(BEGIN_MESSAGE));
    fBeginButton->MakeDefault(true);
    fBeginButton->SetEnabled(false);

    fLaunchDriveSetupButton = new BButton("setup_button",
        B_TRANSLATE("Set up partitions" B_UTF8_ELLIPSIS),
        new BMessage(LAUNCH_DRIVE_SETUP));

    fLaunchBootManagerItem = new BMenuItem(B_TRANSLATE("Set up boot menu" B_UTF8_ELLIPSIS),
        new BMessage(LAUNCH_BOOTMAN));
    fLaunchBootManagerItem->SetEnabled(false);

    fMakeBootableItem = new BMenuItem(B_TRANSLATE("Write boot sector"),
        new BMessage(MSG_WRITE_BOOT_SECTOR));
    fMakeBootableItem->SetEnabled(false);

    fEFILoaderMenu = new BMenu(B_TRANSLATE("Install EFI loader"));

    BMenuBar* mainMenu = new BMenuBar("main menu");
    BMenu* toolsMenu = new BMenu(B_TRANSLATE("Tools"));
    toolsMenu->AddItem(fLaunchBootManagerItem);
    toolsMenu->AddItem(fMakeBootableItem);
    toolsMenu->AddItem(fEFILoaderMenu);
    mainMenu->AddItem(toolsMenu);

    BGroupView* packagesGroup = new BGroupView(B_VERTICAL, B_USE_ITEM_SPACING);
    packagesGroup->AddChild(fPackagesSwitch);
    packagesGroup->AddChild(packagesScrollView);
    packagesGroup->AddChild(fProgressBar);
    packagesGroup->AddChild(fSizeView);

    // -----------------------------------------------------------------
    // CONTENITORE 1: Interfaccia standard dell'Installer
    // -----------------------------------------------------------------
    BGroupView* mainInstallerContainer = new BGroupView(B_VERTICAL, 0);
    BLayoutBuilder::Group<BGroupView>(mainInstallerContainer)
        .Add(mainMenu)
        .Add(fLogoGroup)
        .Add(new BSeparatorView(B_HORIZONTAL, B_PLAIN_BORDER))
        .AddGroup(B_VERTICAL, B_USE_ITEM_SPACING)
            .SetInsets(B_USE_WINDOW_SPACING)
            .AddGrid(new BGridView(B_USE_ITEM_SPACING, B_USE_ITEM_SPACING))
                .AddMenuField(fSrcMenuField, 0, 0)
                .AddMenuField(fDestMenuField, 0, 1)
                .AddGlue(2, 0, 1, 2)
                .Add(BSpaceLayoutItem::CreateVerticalStrut(5), 0, 3, 3)
            .End()
            .Add(packagesGroup)
            .AddGroup(B_HORIZONTAL, B_USE_WINDOW_SPACING)
                .Add(fLaunchDriveSetupButton)
                .AddGlue()
                .Add(fBeginButton)
            .End()
        .End();

    // Estraiamo i riferimenti dei layout item per poterli nascondere/mostrare dinamitamente
    fPackagesLayoutItem = layout_item_for(packagesScrollView);
    fPkgSwitchLayoutItem = layout_item_for(fPackagesSwitch);
    fSizeViewLayoutItem = layout_item_for(fSizeView);
    fProgressLayoutItem = layout_item_for(fProgressBar);

    fPackagesLayoutItem->SetVisible(false);
    fSizeViewLayoutItem->SetVisible(false);
    fProgressLayoutItem->SetVisible(false);

    // -----------------------------------------------------------------
    // CONTENITORE 2: Interfaccia per la Master Password (Overlay)
    // -----------------------------------------------------------------
    BFont titleFont(be_bold_font);
    titleFont.SetSize(titleFont.Size() * 1.8f);

    BStringView* mpTitle = new BStringView("mpTitle",
        B_TRANSLATE("Insert Master Password"));
    mpTitle->SetFont(&titleFont);
    mpTitle->SetExplicitAlignment(
        BAlignment(B_ALIGN_HORIZONTAL_CENTER, B_ALIGN_VERTICAL_UNSET));

	fMasterPassword1 = new BTextControl(B_TRANSLATE("Password:"), "",
        new BMessage(PASSWORD_UPDATED));
	fMasterPassword1->Mask(true);
	fMasterPassword2 = new BTextControl(B_TRANSLATE("Repeat Password:"), "",
        new BMessage(PASSWORD_UPDATED));
	fMasterPassword2->Mask(true);

    BButton* mpShow1 = new BButton("mpShow1", B_TRANSLATE("Show"),
        new BMessage(MASTER_PASSWORD_SHOW));
    BButton* mpShow2 = new BButton("mpShow2", B_TRANSLATE("Show"),
        new BMessage(MASTER_PASSWORD_SHOW));
    BButton* mpSave  = new BButton("mpSave",  B_TRANSLATE("Save"),
        new BMessage(MASTER_PASSWORD_SAVE));

    fMasterPasswordView = new BGroupView("masterPasswordView", B_VERTICAL, 0);
    fMasterPasswordView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

    BLayoutBuilder::Group<>(fMasterPasswordView->GroupLayout())
        .SetInsets(B_USE_WINDOW_SPACING)
        .AddGlue()
        .AddGroup(B_HORIZONTAL)
            .AddGlue()
            .Add(mpTitle)
            .AddGlue()
        .End()
        .AddStrut(B_USE_ITEM_SPACING)
        .AddGroup(B_HORIZONTAL)
            .AddGlue()
            .AddGrid(B_USE_ITEM_SPACING, B_USE_ITEM_SPACING)
                .Add(fMasterPassword1, 0, 0)
                .Add(mpShow1,          1, 0)
                .Add(fMasterPassword2, 0, 1)
                .Add(mpShow2,          1, 1)
            .End()
            .AddGlue()
        .End()
        .AddGlue()
        .AddGroup(B_HORIZONTAL)
            .AddGlue()
            .Add(mpSave)
        .End();

    // -----------------------------------------------------------------
    // SETUP DEL CARD LAYOUT GENERALE SULLA FINESTRA
    // -----------------------------------------------------------------
    fCardLayout = new BCardLayout();
    
    BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
        .Add(fCardLayout)
    .End();

    // Inseriamo i due grandi contenitori dentro il CardLayout
    fCardLayout->AddView(mainInstallerContainer); // Carta indice 0
    fCardLayout->AddView(fMasterPasswordView);    // Carta indice 1

    // Mostriamo l'installer standard all'avvio dell'applicazione
    fCardLayout->SetVisibleItem((int32)0);

    // Finish creating window
    if (!be_roster->IsRunning(kDeskbarSignature))
        SetFlags(Flags() | B_NOT_MINIMIZABLE);

    CenterOnScreen();
    Show();

    // Register to receive notifications when apps launch or quit...
    be_roster->StartWatching(this);
    // ... and check the two we are interested in.
    fDriveSetupLaunched = be_roster->IsRunning(kDriveSetupSignature);
    fBootManagerLaunched = be_roster->IsRunning(kBootManagerSignature);

    if (Lock()) {
        fLaunchDriveSetupButton->SetEnabled(!fDriveSetupLaunched);
        fLaunchBootManagerItem->SetEnabled(!fBootManagerLaunched);
        Unlock();
    }

    PostMessage(START_SCAN);
}


InstallerWindow::~InstallerWindow()
{
	_SetCopyEngineCancelSemaphore(-1);
	be_roster->StopWatching(this);
}


void
InstallerWindow::MessageReceived(BMessage *msg)
{
	switch (msg->what) {
		case MSG_RESET:
		{
			_SetCopyEngineCancelSemaphore(-1);

			status_t error;
			if (msg->FindInt32("error", &error) == B_OK) {
				char errorMessage[2048];
				snprintf(errorMessage, sizeof(errorMessage),
					B_TRANSLATE("An error was encountered and the "
					"installation was not completed:\n\n"
					"Error:  %s"), strerror(error));
				BAlert* alert = new BAlert("error", errorMessage, B_TRANSLATE("OK"));
				alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
				alert->Go();
			}

			_DisableInterface(false);

			fProgressLayoutItem->SetVisible(false);
			fPkgSwitchLayoutItem->SetVisible(true);
			_ShowOptionalPackages();
			_UpdateControls();
			break;
		}
		case START_SCAN:
			_ScanPartitions();
			break;
		case BEGIN_MESSAGE:
			switch (fInstallStatus) {
				case kReadyForInstall:
				{
					// get source and target
					PartitionMenuItem* targetItem
						= (PartitionMenuItem*)fDestMenu->FindMarked();
					PartitionMenuItem* srcItem
						= (PartitionMenuItem*)fSrcMenu->FindMarked();
					if (srcItem == NULL || targetItem == NULL)
						break;

					_SetCopyEngineCancelSemaphore(create_sem(1,
						"copy engine cancel"));

					BList* list = new BList();
					int32 size = 0;
					fPackagesView->GetPackagesToInstall(list, &size);
					fWorkerThread->SetLock(fCopyEngineCancelSemaphore);
					fWorkerThread->SetPackagesList(list);
					fWorkerThread->SetSpaceRequired(size);
					fInstallStatus = kInstalling;
					fWorkerThread->StartInstall(srcItem->ID(),
						targetItem->ID());
					fBeginButton->SetLabel(B_TRANSLATE("Stop"));
					_DisableInterface(true);

					fProgressBar->SetTo(0.0, NULL, NULL);

					fPkgSwitchLayoutItem->SetVisible(false);
					fPackagesLayoutItem->SetVisible(false);
					fSizeViewLayoutItem->SetVisible(false);
					fProgressLayoutItem->SetVisible(true);
					break;
				}
				case kInstalling:
				{
					_QuitCopyEngine(true);
					break;
				}
				case kFinished:
					PostMessage(B_QUIT_REQUESTED);
					break;
				case kCancelled:
					break;
			}
			break;
		case SHOW_BOTTOM_MESSAGE:
			_ShowOptionalPackages();
			break;
		case SOURCE_PARTITION:
			_PublishPackages();
			_UpdateControls();
			break;
		case TARGET_PARTITION:
			_UpdateControls();
			break;
		case EFI_PARTITION:
		{
			partition_id id;
			msg->FindInt32("id", &id);
			fWorkerThread->InstallEFILoader(id, false);
			break;
		}
		case LAUNCH_DRIVE_SETUP:
			_LaunchDriveSetup();
			break;
		case LAUNCH_BOOTMAN:
			_LaunchBootManager();
			break;
		case PACKAGE_CHECKBOX:
		{
			char buffer[15];
			fPackagesView->GetTotalSizeAsString(buffer, sizeof(buffer));
			char string[256];
			snprintf(string, sizeof(string),
				B_TRANSLATE("Additional disk space required: %s"), buffer);
			fSizeView->SetText(string);
			fSizeView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
			break;
		}
		case ENCOURAGE_DRIVESETUP:
		{
			BAlert* alert = new BAlert("use drive setup", B_TRANSLATE("No partitions have "
				"been found that are suitable for installation. Please set "
				"up partitions and format at least one partition with the "
				"Be File System."), B_TRANSLATE("OK"));
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();
			break;
		}
		case MSG_STATUS_MESSAGE:
		{
			float progress;
			if (msg->FindFloat("progress", &progress) == B_OK) {
				const char* currentItem;
				if (msg->FindString("item", &currentItem) != B_OK) {
					currentItem = B_TRANSLATE_COMMENT("???",
						"Unknown currently copied item");
				}
				BString trailingLabel;
				int32 currentCount;
				int32 maximumCount;
				if (msg->FindInt32("current", &currentCount) == B_OK
					&& msg->FindInt32("maximum", &maximumCount) == B_OK) {
					char buffer[64];
					snprintf(buffer, sizeof(buffer),
						B_TRANSLATE_COMMENT("%1ld of %2ld", "number of files copied"),
						(long int)currentCount, (long int)maximumCount);
					trailingLabel << buffer;
				} else {
					trailingLabel <<
						B_TRANSLATE_COMMENT("?? of ??", "Unknown progress");
				}
				fProgressBar->SetTo(progress, currentItem,
					trailingLabel.String());
			} else {
				const char *status;
				if (msg->FindString("status", &status) == B_OK) {
					fLastStatus = fStatusView->Text();
					_SetStatusMessage(status);
				} else
					_SetStatusMessage(fLastStatus.String());
			}
			break;
		}
		case MSG_INSTALL_FINISHED:
		{

			_SetCopyEngineCancelSemaphore(-1);

			PartitionMenuItem* dstItem
				= (PartitionMenuItem*)fDestMenu->FindMarked();

			BString status;
			if (be_roster->IsRunning(kDeskbarSignature)) {
				fBeginButton->SetLabel(B_TRANSLATE("Quit"));

				BString text(B_TRANSLATE("Installation "
					"completed. Boot sector has been written to '%s'. Press "
					"'Quit' to leave the %appname% or choose a new target "
					"volume to perform another installation."));
				text.ReplaceFirst("%appname%", B_TRANSLATE_SYSTEM_NAME("Installer"));
				status.SetToFormat(text, dstItem ? dstItem->Name() : B_TRANSLATE_COMMENT("???",
						"Unknown partition name"));
			} else {
				fBeginButton->SetLabel(B_TRANSLATE("Restart"));
				status.SetToFormat(B_TRANSLATE("Installation "
					"completed. Boot sector has been written to '%s'. Press "
					"'Restart' to restart the computer or choose a new target "
					"volume to perform another installation."),
					dstItem ? dstItem->Name() : B_TRANSLATE_COMMENT("???",
						"Unknown partition name"));
			}

			_SetStatusMessage(status.String());
			fInstallStatus = kFinished;

			_DisableInterface(false);
			fProgressLayoutItem->SetVisible(false);
			fPkgSwitchLayoutItem->SetVisible(true);
			_ShowOptionalPackages();

			// Cover the window with the master password entry overlay.
			//fMasterPasswordView->ResizeTo(Bounds().Width(), Bounds().Height());
			//fMasterPasswordView->Show();
			//fPasswordLayoutItem->SetVisible(true);
			// --- Cambio Schermata Pulito Nativo ---
            // Invece di ResizeTo e Show manuali, diciamo al layout di mostrare la carta 1
            if (fCardLayout != NULL) {
                fCardLayout->SetVisibleItem(1);
                fBeginButton->SetEnabled(false);
            }
			break;
		}
		case B_SOME_APP_LAUNCHED:
		case B_SOME_APP_QUIT:
		{
			const char *signature;
			if (msg->FindString("be:signature", &signature) != B_OK)
				break;
			bool isDriveSetup = !strcasecmp(signature, kDriveSetupSignature);
			bool isBootManager = !strcasecmp(signature, kBootManagerSignature);
			if (isDriveSetup || isBootManager) {
				bool scanPartitions = false;
				if (isDriveSetup) {
					bool launched = msg->what == B_SOME_APP_LAUNCHED;
					// We need to scan partitions if DriveSetup has quit.
					scanPartitions = fDriveSetupLaunched && !launched;
					fDriveSetupLaunched = launched;
				}
				if (isBootManager)
					fBootManagerLaunched = msg->what == B_SOME_APP_LAUNCHED;

				fBeginButton->SetEnabled(
					!fDriveSetupLaunched && !fBootManagerLaunched);
				_DisableInterface(fDriveSetupLaunched || fBootManagerLaunched);
				if (fDriveSetupLaunched && fBootManagerLaunched) {
					_SetStatusMessage(B_TRANSLATE("Running BootManager and "
						"DriveSetup" B_UTF8_ELLIPSIS
						"\n\nClose both applications to continue with the "
						"installation."));
				} else if (fDriveSetupLaunched) {
					_SetStatusMessage(B_TRANSLATE("Running DriveSetup"
						B_UTF8_ELLIPSIS
						"\n\nClose DriveSetup to continue with the "
						"installation."));
				} else if (fBootManagerLaunched) {
					_SetStatusMessage(B_TRANSLATE("Running BootManager"
						B_UTF8_ELLIPSIS
						"\n\nClose BootManager to continue with the "
						"installation."));
				} else {
					// If neither DriveSetup nor Bootman is running, we need
					// to scan partitions in case DriveSetup has quit, or
					// we need to update the guidance message, unless install
					// was already finished.
					if (scanPartitions)
						_ScanPartitions();
					else if (fInstallStatus != kFinished)
						_UpdateControls();
					else
						PostMessage(MSG_INSTALL_FINISHED);
				}
			}
			break;
		}
		case MSG_WRITE_BOOT_SECTOR:
			fWorkerThread->WriteBootSector(fDestMenu);
			break;

		case PASSWORD_UPDATED:
		{
			// Trigger DrawAfterChildren to keep the '*' overlay in sync.
			//if (fMasterPassword1 != NULL && fMasterPassword1->TextView() != NULL) {
			//	fMasterPassword1->TextView()->Invalidate();
			//	fMasterPassword1->Invalidate();
			//}
			//if (fMasterPassword2 != NULL && fMasterPassword2->TextView() != NULL) {
			//	fMasterPassword2->TextView()->Invalidate();
			//	fMasterPassword2->Invalidate();
			//}
			break;
		}

		case MASTER_PASSWORD_SHOW:
		{
	    if (fMasterPassword1 != NULL)
    	    fMasterPassword1->Mask(!fMasterPassword1->IsMasked());

	    if (fMasterPassword2 != NULL)
        	fMasterPassword2->Mask(!fMasterPassword2->IsMasked());
        
    	break;
	}
		/*{
			// Toggle masked/visible on both fields simultaneously.
			bool showText = fMasterPassword1->IsMasked();

			if (fMasterPassword1 != NULL) {
				fMasterPassword1->Mask(!showText);
				// Ripristiniamo la selezione o forziamo il ridisegno pulito
				if (fMasterPassword1->TextView() != NULL)
					fMasterPassword1->TextView()->Invalidate();
			}

			if (fMasterPassword2 != NULL) {
				fMasterPassword2->Mask(!showText);
				if (fMasterPassword2->TextView() != NULL)
					fMasterPassword2->TextView()->Invalidate();
			}
			break;
		}*/
		case MASTER_PASSWORD_SAVE:
		{
			uint8 salt[16];
			status_t err = _WriteMasterPasswordShadow(salt);
			if (err == B_OK)
				err = _WriteKeystore(salt);
			else {
				memset(salt, 0, sizeof(salt));
				BAlert* alert = new BAlert("shadowError",
					B_TRANSLATE("Failed to save master password shadow file."),
					B_TRANSLATE("OK"));
				alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
				alert->Go();
				break;
			}
			
			memset(salt, 0, sizeof(salt));

			if (err != B_OK) {
				BAlert* alert = new BAlert("shadowError",
					B_TRANSLATE("Failed to store encryption keys."),
					B_TRANSLATE("OK"));
				alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
				alert->Go();
				break;
			}
			
			if (fCardLayout != NULL) {
                fCardLayout->SetVisibleItem(0);
                fBeginButton->SetEnabled(true);
            }
			break;
		}

		default:
			BWindow::MessageReceived(msg);
			break;
	}
}


void
InstallerWindow::FrameResized(float newWidth, float newHeight)
{
	BWindow::FrameResized(newWidth, newHeight);
	// Keep the master password overlay covering the entire client area.
	if (fMasterPasswordView != NULL)
		fMasterPasswordView->ResizeTo(newWidth, newHeight);
}


status_t
InstallerWindow::_WriteMasterPasswordShadow(uint8* outSalt)
{
	fprintf(stderr, "[Installer Shadow] Inizio procedura _WriteMasterPasswordShadow\n");
	// --- 1. Resolve destination partition mount point ---
	PartitionMenuItem* dstItem
		= (PartitionMenuItem*)fDestMenu->FindMarked();
	if (dstItem == NULL){
		fprintf(stderr, "[Installer Shadow] ERRORE: Nessuna partizione di destinazione contrassegnata.\n");
		return B_BAD_VALUE;
	}

	BDiskDeviceRoster roster;
	BDiskDevice device;
	BPartition* partition = NULL;

	status_t err = roster.GetPartitionWithID(dstItem->ID(), &device, &partition);
	if (err != B_OK){
		fprintf(stderr, "[Installer Shadow] ERRORE GetPartitionWithID: %s (0x%" B_PRIx32 ")\n", strerror(err), err);
		return err;
	}

	BPath destRoot;
	if (partition != NULL)
		err = partition->GetMountPoint(&destRoot);
	else
		err = device.GetMountPoint(&destRoot);
	if (err != B_OK){
		fprintf(stderr, "[Installer Shadow] ERRORE GetMountPoint: %s (0x%" B_PRIx32 ")\n", strerror(err), err);
		return err;
	}
	
    fprintf(stderr, "[Installer Shadow] Mount point destinazione: %s\n", destRoot.Path());
	// --- 2. Ensure settings directory exists ---
	BPath settingsDir(destRoot.Path(), "home/config/settings");
	err = create_directory(settingsDir.Path(), 0755);
	if (err != B_OK && err != B_FILE_EXISTS){
		fprintf(stderr, "[Installer Shadow] ERRORE create_directory (%s): %s\n", settingsDir.Path(), strerror(err));
		return err;
	}

	// --- 3. Generate 16-byte random salt ---
	BCrypto crypto;
	if (crypto.InitCheck() != B_OK){
		fprintf(stderr, "[Installer Shadow] ERRORE: Inizializzazione BCrypto fallita.\n");
		return B_ERROR;
	}

	uint8 salt[16];
	err = crypto.GetRandomBytes(salt, sizeof(salt));
	if (err != B_OK){
		fprintf(stderr, "[Installer Shadow] ERRORE GetRandomBytes: %s\n", strerror(err));
		return err;
	}

	// Copy salt out before any zeroing so the caller can reuse it.
	memcpy(outSalt, salt, sizeof(salt));

	// --- 4. Compute blake2b(password || salt) ---
	// Concatenate password bytes and salt into a single input buffer.
	const char* password = fMasterPassword1->Text();
	size_t passLen = strlen(password);
	fprintf(stderr, "[Installer Shadow] Lunghezza password recuperata: %zu caratteri\n", passLen);
	
	size_t inputLen = passLen + sizeof(salt);

	uint8* input = new(std::nothrow) uint8[inputLen];
	if (input == NULL) {
		memset(salt, 0, sizeof(salt));
		return B_NO_MEMORY;
	}
	memcpy(input, password, passLen);
	memcpy(input + passLen, salt, sizeof(salt));

	// blake2b always produces 64 bytes
	uint8 hash[64];
	size_t hashLen = crypto.GetHashLength(B_CRYPTO_BLAKE2B);
	err = crypto.Digest(B_CRYPTO_BLAKE2B, input, inputLen, hash);

	// Zero sensitive data as early as possible
	memset(input, 0, inputLen);
	delete[] input;

	if (err != B_OK) {
		fprintf(stderr, "[Installer Shadow] ERRORE crypto.Digest (BLAKE2B): %s\n", strerror(err));
		memset(salt, 0, sizeof(salt));
		memset(hash, 0, sizeof(hash));
		return err;
	}

	// --- 5. Build BMessage and flatten to file ---
	BMessage shadow;
	shadow.AddData("salt", B_RAW_TYPE, salt, sizeof(salt));
	shadow.AddData("hash", B_RAW_TYPE, hash, hashLen);

	memset(salt, 0, sizeof(salt));
	memset(hash, 0, sizeof(hash));

	BPath shadowPath(settingsDir.Path(), "shadow");
	BFile shadowFile(shadowPath.Path(),
		B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (shadowFile.InitCheck() != B_OK){
		status_t initErr = shadowFile.InitCheck();
		fprintf(stderr, "[Installer Shadow] ERRORE Inizializzazione BFile (%s): %s (0x%" B_PRIx32 ")\n", 
				shadowPath.Path(), strerror(initErr), initErr);
		return initErr;
    }

	err = shadow.Flatten(&shadowFile);
    if (err != B_OK) {
        fprintf(stderr, "[Installer Shadow] ERRORE Flattening BMessage shadow: %s\n", strerror(err));
    } else {
        fprintf(stderr, "[Installer Shadow] File shadow salvato con successo in %s!\n", shadowPath.Path());
    }

    return err;
}
// 
/* Con BCrypto, non funziona da capire dove
status_t
InstallerWindow::_WriteKeystore(const uint8* salt)
{
	fprintf(stderr, "[Keystore] Inizio procedura _WriteKeystore...\n");
	// --- 1. Resolve destination mount point ---
	PartitionMenuItem* dstItem
		= (PartitionMenuItem*)fDestMenu->FindMarked();
	if (dstItem == NULL){
		fprintf(stderr, "[Keystore] ERRORE: dstItem è NULL (Nessuna partizione selezionata).\n");
		return B_BAD_VALUE;
	}

	BDiskDeviceRoster roster;
	BDiskDevice device;
	BPartition* partition = NULL;

	status_t err = roster.GetPartitionWithID(dstItem->ID(), &device, &partition);
	if (err != B_OK){
		fprintf(stderr, "[Keystore] ERRORE GetPartitionWithID: %s (0x%" B_PRIx32 ")\n", strerror(err), err);
		return err;
	}

	BPath destRoot;
	if (partition != NULL)
		err = partition->GetMountPoint(&destRoot);
	else
		err = device.GetMountPoint(&destRoot);
	if (err != B_OK){
		fprintf(stderr, "[Keystore] ERRORE GetMountPoint: %s (0x%" B_PRIx32 ")\n", strerror(err), err);
		return err;
	}
	fprintf(stderr, "[Keystore] Target mount point: %s\n", destRoot.Path());

	// --- 2. Ensure keystore directory exists ---
	BPath keystoreDir(destRoot.Path(), "home/config/settings/system/keystore");
	err = create_directory(keystoreDir.Path(), 0755);
	if (err != B_OK && err != B_FILE_EXISTS) {
		fprintf(stderr, "[Keystore] ERRORE create_directory in %s: %s\n", keystoreDir.Path(), strerror(err));
		return err;
	}

	BPath keyPath(keystoreDir.Path(), "master");

	// --- 3. Derive 32-byte AES-256 key via BCrypto: SHA-256(pwd||salt) × 1000 ---
	BCrypto crypto;
	if (crypto.InitCheck() != B_OK) {
		fprintf(stderr, "[Keystore] ERRORE: InitCheck di BCrypto fallito nel keystore.\n");
		return B_ERROR;
	}

	const char* password = fMasterPassword1->Text();
	size_t passLen = strlen(password);
	size_t inputLen = passLen + 16;

	uint8* kdfInput = new(std::nothrow) uint8[inputLen];
	if (kdfInput == NULL) {
		fprintf(stderr, "[Keystore] ERRORE: Out of memory allocando kdfInput.\n");
		return B_NO_MEMORY;
	}
	memcpy(kdfInput, password, passLen);
	memcpy(kdfInput + passLen, salt, 16);

	uint8 aesKey[32];
	err = crypto.Digest(B_CRYPTO_SHA256, kdfInput, inputLen, aesKey);
	memset(kdfInput, 0, inputLen);
	delete[] kdfInput;
	if (err != B_OK) {
		fprintf(stderr, "[Keystore] ERRORE: Primo digest SHA-256 fallito: %s\n", strerror(err));
		memset(aesKey, 0, sizeof(aesKey));
		return err;
	}

	for (int i = 1; i < 1000; i++) {
		err = crypto.Digest(B_CRYPTO_SHA256, aesKey, sizeof(aesKey), aesKey);
		if (err != B_OK) {
			fprintf(stderr, "[Keystore] ERRORE: Digest iterativo fallito all'indice %d: %s\n", i, strerror(err));
			memset(aesKey, 0, sizeof(aesKey));
			return err;
		}
	}
	fprintf(stderr, "[Keystore] Chiave AES-256 derivata con successo.\n");

	// --- 4. Generate RSA-2048 key pair via OpenSSL ---
	EVP_PKEY* pkey = NULL;
	{
		EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
		if (ctx == NULL
				|| EVP_PKEY_keygen_init(ctx) <= 0
				|| EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0
				|| EVP_PKEY_keygen(ctx, &pkey) <= 0) {
			fprintf(stderr, "[Keystore] ERRORE: Generazione coppia RSA fallita via OpenSSL.\n");
			ERR_print_errors_fp(stderr);
			EVP_PKEY_CTX_free(ctx);
			memset(aesKey, 0, sizeof(aesKey));
			return B_ERROR;
		}
		EVP_PKEY_CTX_free(ctx);
	}
	fprintf(stderr, "[Keystore] Coppia di chiavi RSA-2048 generata.\n");

	// --- 5. Extract public key (DER SubjectPublicKeyInfo) ---
	unsigned char* pubDer = NULL;
	int pubLen = i2d_PUBKEY(pkey, &pubDer);
	if (pubLen <= 0 || pubDer == NULL) {
		fprintf(stderr, "[Keystore] ERRORE: Estrazione chiave pubblica (DER) fallita.\n");
		EVP_PKEY_free(pkey);
		memset(aesKey, 0, sizeof(aesKey));
		return B_ERROR;
	}

	// --- 6. Extract private key (DER PrivateKeyInfo) ---
	unsigned char* privDer = NULL;
	int privLen = i2d_PrivateKey(pkey, &privDer);
	EVP_PKEY_free(pkey);
	if (privLen <= 0 || privDer == NULL) {
		fprintf(stderr, "[Keystore] ERRORE: Estrazione chiave privata (DER) fallita.\n");
		OPENSSL_free(pubDer);
		memset(aesKey, 0, sizeof(aesKey));
		return B_ERROR;
	}

	// --- 7. Encrypt private key with BCrypto AES-256-CBC-PKCS7 ---
	uint8 iv[16];
	err = crypto.GetRandomBytes(iv, sizeof(iv));
	if (err != B_OK) {
		fprintf(stderr, "[Keystore] ERRORE crypto.GetRandomBytes per IV: %s\n", strerror(err));
		OPENSSL_cleanse(privDer, privLen);
		OPENSSL_free(privDer);
		OPENSSL_free(pubDer);
		memset(aesKey, 0, sizeof(aesKey));
		return err;
	}

	crypto.SetAlgorithm(B_CRYPTO_AES);
	crypto.SetMode(B_CRYPTO_MODE_CBC);
	crypto.SetPadding(true, B_CRYPTO_PKCS7);

	size_t encBufSize = crypto.GetOutputSize(privLen, B_CRYPTO_ENCRYPT);
	uint8* encPriv = new(std::nothrow) uint8[encBufSize];
	if (encPriv == NULL) {
		fprintf(stderr, "[Keystore] ERRORE: Out of memory allocando encPriv.\n");
		OPENSSL_cleanse(privDer, privLen);
		OPENSSL_free(privDer);
		OPENSSL_free(pubDer);
		memset(aesKey, 0, sizeof(aesKey));
		return B_NO_MEMORY;
	}

	ssize_t encLen = crypto.Encrypt(aesKey, sizeof(aesKey), iv, sizeof(iv),
		privDer, privLen, encPriv, encBufSize);

	OPENSSL_cleanse(privDer, privLen);
	OPENSSL_free(privDer);
	memset(aesKey, 0, sizeof(aesKey));

	if (encLen < 0) {
		fprintf(stderr, "[Keystore] ERRORE crypto.Encrypt fallito (codice ritornato: %zd).\n", encLen);
		memset(encPriv, 0, encBufSize);
		delete[] encPriv;
		OPENSSL_free(pubDer);
		return B_ERROR;
	}
	fprintf(stderr, "[Keystore] Chiave privata cifrata con successo. Dimensione: %zd bytes.\n", encLen);

	// --- 8. Write public key (in clear) and encrypted private key as attribute ---
	BFile keyFile(keyPath.Path(), B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
	if (keyFile.InitCheck() != B_OK) {
		err = keyFile.InitCheck();
		fprintf(stderr, "[Keystore] ERRORE Inizializzazione BFile in %s: %s (0x%" B_PRIx32 ")\n", keyPath.Path(),
				strerror(err), err);
		OPENSSL_free(pubDer);
		memset(encPriv, 0, encBufSize);
		delete[] encPriv;
		return err;
	}

	// File content: DER-encoded SubjectPublicKeyInfo (public key in clear)
	ssize_t written = keyFile.Write(pubDer, pubLen);
	fprintf(stderr, "[Keystore] Scritto file master in chiaro (%zd di %d bytes).\n", written, pubLen);
	OPENSSL_free(pubDer);

	// Attribute: IV (16 bytes) || BCrypto-AES-256-CBC(DER private key)
	size_t attrSize = sizeof(iv) + encLen;
	uint8* attrData = new(std::nothrow) uint8[attrSize];
	if (attrData == NULL) {
		fprintf(stderr, "[Keystore] ERRORE: Out of memory allocando attrData.\n");
		memset(encPriv, 0, encBufSize);
		delete[] encPriv;
		return B_NO_MEMORY;
	}
	memcpy(attrData, iv, sizeof(iv));
	memcpy(attrData + sizeof(iv), encPriv, encLen);

	memset(encPriv, 0, encBufSize);
	delete[] encPriv;

	ssize_t attrWritten = keyFile.WriteAttr("crypto:private_key", B_RAW_TYPE, 0, attrData, attrSize);
	if (attrWritten < 0) {
		fprintf(stderr, "[Keystore] ERRORE grave nello scrivere l'attributo esteso (WriteAttr ritarda: %zd, errore: %s)\n", attrWritten, strerror(attrWritten));
		err = attrWritten;
	} else {
		fprintf(stderr, "[Keystore] Attributo esteso 'crypto:private_key' scritto correttamente (%zd bytes).\n", attrWritten);
		err = B_OK;
	}

	memset(attrData, 0, attrSize);
	delete[] attrData;

	return B_OK;
}*/
status_t
InstallerWindow::_WriteKeystore(const uint8* salt)
{
	fprintf(stderr, "[Keystore] Inizio procedura _WriteKeystore...\n");
	// --- 1. Resolve destination mount point ---
	PartitionMenuItem* dstItem
		= (PartitionMenuItem*)fDestMenu->FindMarked();
	if (dstItem == NULL){
		fprintf(stderr, "[Keystore] ERRORE: dstItem è NULL (Nessuna partizione selezionata).\n");
		return B_BAD_VALUE;
	}

	BDiskDeviceRoster roster;
	BDiskDevice device;
	BPartition* partition = NULL;

	status_t err = roster.GetPartitionWithID(dstItem->ID(), &device, &partition);
	if (err != B_OK){
		fprintf(stderr, "[Keystore] ERRORE GetPartitionWithID: %s (0x%" B_PRIx32 ")\n", strerror(err), err);
		return err;
	}

	BPath destRoot;
	if (partition != NULL)
		err = partition->GetMountPoint(&destRoot);
	else
		err = device.GetMountPoint(&destRoot);
	if (err != B_OK){
		fprintf(stderr, "[Keystore] ERRORE GetMountPoint: %s (0x%" B_PRIx32 ")\n", strerror(err), err);
		return err;
	}
	fprintf(stderr, "[Keystore] Target mount point: %s\n", destRoot.Path());

	// --- 2. Ensure keystore directory exists ---
	BPath keystoreDir(destRoot.Path(), "home/config/settings/system/keystore");
	err = create_directory(keystoreDir.Path(), 0755);
	if (err != B_OK && err != B_FILE_EXISTS) {
		fprintf(stderr, "[Keystore] ERRORE create_directory in %s: %s\n", keystoreDir.Path(), strerror(err));
		return err;
	}

	BPath keyPath(keystoreDir.Path(), "master");
	
	// ==========================================
    // 1. KDF MANUALE CON OPENSSL (1000 ROUND SHA256)
    // ==========================================
    const char* password = fMasterPassword1->Text();
    size_t passLen = strlen(password);
    size_t inputLen = passLen + 16;

    uint8* kdfInput = new(std::nothrow) uint8[inputLen];
    if (kdfInput == NULL) return B_NO_MEMORY;
    
    memcpy(kdfInput, password, passLen);
    memcpy(kdfInput + passLen, salt, 16);

    uint8 aesKey[32];
    unsigned int mdLen = 0;
    
    // Primo round SHA256
    EVP_Digest(kdfInput, inputLen, aesKey, &mdLen, EVP_sha256(), NULL);
    OPENSSL_cleanse(kdfInput, inputLen);
    delete[] kdfInput;

    // Restanti 999 round
    for (int i = 1; i < 1000; i++) {
        EVP_Digest(aesKey, 32, aesKey, &mdLen, EVP_sha256(), NULL);
    }

    // ==========================================
    // 2. GENERAZIONE COPPIA DI CHIAVI RSA-2048
    // ==========================================
    EVP_PKEY* pkey = NULL;
    EVP_PKEY_CTX* kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (kctx == NULL
            || EVP_PKEY_keygen_init(kctx) <= 0
            || EVP_PKEY_CTX_set_rsa_keygen_bits(kctx, 2048) <= 0
            || EVP_PKEY_keygen(kctx, &pkey) <= 0) {
        EVP_PKEY_CTX_free(kctx);
        OPENSSL_cleanse(aesKey, sizeof(aesKey));
        return B_ERROR;
    }
    EVP_PKEY_CTX_free(kctx);

    // Estrarre Chiave Pubblica in formato DER nudo
    unsigned char* pubDer = NULL;
    int pubLen = i2d_PUBKEY(pkey, &pubDer);
    if (pubLen <= 0 || pubDer == NULL) {
        EVP_PKEY_free(pkey);
        OPENSSL_cleanse(aesKey, sizeof(aesKey));
        return B_ERROR;
    }

    // Estrarre Chiave Privata in formato DER nudo
    unsigned char* privDer = NULL;
    int privLen = i2d_PrivateKey(pkey, &privDer);
    EVP_PKEY_free(pkey);
    if (privLen <= 0 || privDer == NULL) {
        OPENSSL_free(pubDer);
        OPENSSL_cleanse(aesKey, sizeof(aesKey));
        return B_ERROR;
    }

    // ==========================================
    // 3. CIFRATURA SIMMETRICA AES-256-CBC CON OPENSSL
    // ==========================================
    uint8 iv[16];
    if (RAND_bytes(iv, sizeof(iv)) != 1) {
        OPENSSL_cleanse(privDer, privLen);
        OPENSSL_free(privDer);
        OPENSSL_free(pubDer);
        OPENSSL_cleanse(aesKey, sizeof(aesKey));
        return B_ERROR;
    }

    // Allocazione buffer ciphertext (Dimensione massima = plaintext + blocco AES)
    int maxEncLen = privLen + 16; 
    uint8* encPriv = new(std::nothrow) uint8[maxEncLen];
    if (encPriv == NULL) {
        OPENSSL_cleanse(privDer, privLen);
        OPENSSL_free(privDer);
        OPENSSL_free(pubDer);
        OPENSSL_cleanse(aesKey, sizeof(aesKey));
        return B_NO_MEMORY;
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int len = 0;
    int encLen = 0;

    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, aesKey, iv);
    EVP_CIPHER_CTX_set_padding(ctx, 1); // Abilita PKCS7

    if (EVP_EncryptUpdate(ctx, encPriv, &len, privDer, privLen) != 1) {
        fprintf(stderr, "[DEBUG WRITE] Errore in EncryptUpdate\n");
    }
    encLen = len;

    if (EVP_EncryptFinal_ex(ctx, encPriv + len, &len) != 1) {
        fprintf(stderr, "[DEBUG WRITE] Errore in EncryptFinal\n");
        encLen = -1;
    } else {
        encLen += len;
    }

    EVP_CIPHER_CTX_free(ctx);
    OPENSSL_cleanse(privDer, privLen);
    OPENSSL_free(privDer);
    OPENSSL_cleanse(aesKey, sizeof(aesKey));

    if (encLen < 0) {
        delete[] encPriv;
        OPENSSL_free(pubDer);
        return B_ERROR;
    }

    // ==========================================
    // 4. SCRITTURA SU DISCO (FILE + ATTRIBUTO BFS)
    // ==========================================
    BFile keyFile(keyPath.Path(), B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
    if (keyFile.InitCheck() != B_OK) {
        err = keyFile.InitCheck();
        OPENSSL_free(pubDer);
        delete[] encPriv;
        return err;
    }

    // Scrittura della chiave pubblica nel corpo del file
    keyFile.Write(pubDer, pubLen);
    OPENSSL_free(pubDer);

    // Preparazione pacchetto BFS: IV (16) + Ciphertext (encLen)
    size_t attrSize = sizeof(iv) + encLen;
    uint8* attrData = new(std::nothrow) uint8[attrSize];
    if (attrData == NULL) {
        delete[] encPriv;
        return B_NO_MEMORY;
    }
    
    memcpy(attrData, iv, sizeof(iv));
    memcpy(attrData + sizeof(iv), encPriv, encLen);
    delete[] encPriv;

    ssize_t attrWritten = keyFile.WriteAttr("crypto:private_key", B_RAW_TYPE, 0, attrData, attrSize);
    if (attrWritten < 0) {
        err = attrWritten;
    } else {
        err = B_OK;
        fprintf(stderr, "[DEBUG WRITE] Scrittura completata. PrivKey DER puro cifrato!\n");
    }

    OPENSSL_cleanse(attrData, attrSize);
    delete[] attrData;

    return err;
}


bool
InstallerWindow::QuitRequested()
{
	if ((Flags() & B_NOT_MINIMIZABLE) != 0) {
		// This means Deskbar is not running, i.e. Installer is the only
		// thing on the screen and we will reboot the machine once it quits.

		if (fDriveSetupLaunched && fBootManagerLaunched) {
			BAlert* alert = new BAlert(B_TRANSLATE("Quit BootManager and "
				"DriveSetup"),	B_TRANSLATE("Please close the BootManager "
				"and DriveSetup windows before closing the Installer window."),
				B_TRANSLATE("OK"));
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();
			return false;
		}
		if (fDriveSetupLaunched) {
			BAlert* alert = new BAlert(B_TRANSLATE("Quit DriveSetup"),
				B_TRANSLATE("Please close the DriveSetup window before "
				"closing the Installer window."), B_TRANSLATE("OK"));
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();
			return false;
		}
		if (fBootManagerLaunched) {
			BAlert* alert = new BAlert(B_TRANSLATE("Quit BootManager"),
				B_TRANSLATE("Please close the BootManager window before "
				"closing the Installer window."), B_TRANSLATE("OK"));
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();
			return false;
		}
		if (fInstallStatus != kFinished) {
			BAlert* alert = new BAlert(B_TRANSLATE_SYSTEM_NAME("Installer"),
				B_TRANSLATE("Are you sure you want to stop the installation?"),
				B_TRANSLATE("Cancel"), B_TRANSLATE("Stop"), NULL,
				B_WIDTH_AS_USUAL, B_STOP_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			if (alert->Go() == 0)
				return false;
		}
	} else if (fInstallStatus == kInstalling) {
			BAlert* alert = new BAlert(B_TRANSLATE_SYSTEM_NAME("Installer"),
				B_TRANSLATE("The installation is not complete yet!\n"
                                "Are you sure you want to stop it?"),
				B_TRANSLATE("Cancel"), B_TRANSLATE("Stop"), NULL,
				B_WIDTH_AS_USUAL, B_STOP_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			if (alert->Go() == 0)
				return false;
	}

	_QuitCopyEngine(false);

	BMessage quitWithInstallStatus(B_QUIT_REQUESTED);
	quitWithInstallStatus.AddBool("install_complete",
		fInstallStatus == kFinished);

	fWorkerThread->PostMessage(&quitWithInstallStatus);
	be_app->PostMessage(&quitWithInstallStatus);
	return true;
}


// #pragma mark -


void
InstallerWindow::_ShowOptionalPackages()
{
	if (fPackagesLayoutItem && fSizeViewLayoutItem) {
		fPackagesLayoutItem->SetVisible(fPackagesSwitch->Value());
		fSizeViewLayoutItem->SetVisible(fPackagesSwitch->Value());
	}
}


void
InstallerWindow::_LaunchDriveSetup()
{
	if (be_roster->Launch(kDriveSetupSignature) != B_OK) {
		// Try really hard to launch it. It's very likely that this fails,
		// when we run from the CD and there is only an incomplete mime
		// database for example...
		BPath path;
		if (find_directory(B_SYSTEM_APPS_DIRECTORY, &path) != B_OK
			|| path.Append("DriveSetup") != B_OK) {
			path.SetTo("/boot/system/apps/DriveSetup");
		}
		BEntry entry(path.Path());
		entry_ref ref;
		if (entry.GetRef(&ref) != B_OK || be_roster->Launch(&ref) != B_OK) {
			BAlert* alert = new BAlert("error", B_TRANSLATE("DriveSetup, the "
				"application to configure disk partitions, could not be "
				"launched."), B_TRANSLATE("OK"));
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();
		}
	}
}


void
InstallerWindow::_LaunchBootManager()
{
	// TODO: Currently BootManager always tries to install to the "first"
	// harddisk. If/when it later supports being installed to a certain
	// harddisk, we would have to pass it the disk that contains the target
	// partition here.
	if (be_roster->Launch(kBootManagerSignature) != B_OK) {
		// Try really hard to launch it. It's very likely that this fails,
		// when we run from the CD and there is only an incomplete mime
		// database for example...
		BPath path;
		if (find_directory(B_SYSTEM_APPS_DIRECTORY, &path) != B_OK
			|| path.Append("BootManager") != B_OK) {
			path.SetTo("/boot/system/apps/BootManager");
		}
		BEntry entry(path.Path());
		entry_ref ref;
		if (entry.GetRef(&ref) != B_OK || be_roster->Launch(&ref) != B_OK) {
			BAlert* alert = new BAlert(
				B_TRANSLATE("Failed to launch BootManager"),
				B_TRANSLATE("BootManager, the application to configure the "
					"Haiku boot menu, could not be launched."),
				B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();
		}
	}
}


void
InstallerWindow::_DisableInterface(bool disable)
{
	fLaunchDriveSetupButton->SetEnabled(!disable);
	fLaunchBootManagerItem->SetEnabled(!disable);
	fMakeBootableItem->SetEnabled(!disable);
	fSrcMenuField->SetEnabled(!disable);
	fDestMenuField->SetEnabled(!disable);
}


void
InstallerWindow::_ScanPartitions()
{
	_SetStatusMessage(B_TRANSLATE("Scanning for disks" B_UTF8_ELLIPSIS));

	BMenuItem *item;
	while ((item = fSrcMenu->RemoveItem((int32)0)))
		delete item;
	while ((item = fDestMenu->RemoveItem((int32)0)))
		delete item;
	while ((item = fEFILoaderMenu->RemoveItem((int32)0)))
		delete item;

	fWorkerThread->ScanDisksPartitions(fSrcMenu, fDestMenu, fEFILoaderMenu);

	if (fSrcMenu->ItemAt(0) != NULL)
		_PublishPackages();

	if (fEFILoaderMenu->ItemAt(0) == NULL) {
		BMenuItem* noPart = new BMenuItem(B_TRANSLATE("No valid EFI system data partitions found"),
			NULL);
		noPart->SetEnabled(false);
		fEFILoaderMenu->AddItem(noPart);
	}

	// If the install is already finished, keep the button as is.
	if (fInstallStatus != kFinished)
		_UpdateControls();
	else
		PostMessage(MSG_INSTALL_FINISHED);
}


void
InstallerWindow::_UpdateControls()
{
	PartitionMenuItem* srcItem = (PartitionMenuItem*)fSrcMenu->FindMarked();
	BString label;
	if (srcItem) {
		label = srcItem->MenuLabel();
	} else {
		if (fSrcMenu->CountItems() == 0)
			label = B_TRANSLATE_COMMENT("<none>", "No partition available");
		else
			label = ((PartitionMenuItem*)fSrcMenu->ItemAt(0))->MenuLabel();
	}
	fSrcMenuField->MenuItem()->SetLabel(label.String());

	// Disable any unsuitable target items, check if at least one partition
	// is suitable.
	bool foundOneSuitableTarget = false;
	for (int32 i = fDestMenu->CountItems() - 1; i >= 0; i--) {
		PartitionMenuItem* dstItem
			= (PartitionMenuItem*)fDestMenu->ItemAt(i);
		if (srcItem != NULL && dstItem->ID() == srcItem->ID()) {
			// Prevent the user from having picked the same partition as source
			// and destination.
			dstItem->SetEnabled(false);
			dstItem->SetMarked(false);
		} else
			dstItem->SetEnabled(dstItem->IsValidTarget());

		if (dstItem->IsEnabled())
			foundOneSuitableTarget = true;
	}

	PartitionMenuItem* dstItem = (PartitionMenuItem*)fDestMenu->FindMarked();
	if (dstItem) {
		label = dstItem->MenuLabel();
	} else {
		if (fDestMenu->CountItems() == 0)
			label = B_TRANSLATE_COMMENT("<none>", "No partition available");
		else
			label = B_TRANSLATE("Please choose target");
	}
	fDestMenuField->MenuItem()->SetLabel(label.String());

	BString statusText;
	if (srcItem != NULL && dstItem != NULL) {
		statusText.SetToFormat(B_TRANSLATE("Press the 'Begin' button to install "
			"from '%1s' onto '%2s'."), srcItem->Name(), dstItem->Name());
	} else if (srcItem != NULL) {
		BString partitionRequiredHaiku = B_TRANSLATE(
			"Haiku has to be installed on a partition that uses "
			"the Be File System, but there are currently no such "
			"partitions available on your system.");

		BString partitionRequiredDebranded = B_TRANSLATE(
			"This operating system has to be installed on a partition "
			"that uses the Be File System, but there are currently "
			"no such partitions available on your system.");

		if (!foundOneSuitableTarget) {
#ifdef HAIKU_DISTRO_COMPATIBILITY_OFFICIAL
			statusText.Append(partitionRequiredHaiku);
#else
			statusText.Append(partitionRequiredDebranded);
#endif
			statusText.Append(" ");
			statusText.Append(B_TRANSLATE(
				"Click on 'Set up partitions" B_UTF8_ELLIPSIS
				"' to create one."));
		} else {
			statusText = B_TRANSLATE(
				"Choose the disk you want to install "
				"onto from the pop-up menu. Then click 'Begin'.");
		}
	} else if (dstItem != NULL) {
		statusText = B_TRANSLATE("Choose the source disk from the "
			"pop-up menu. Then click 'Begin'.");
	} else {
		statusText = B_TRANSLATE("Choose the source and destination disk "
			"from the pop-up menus. Then click 'Begin'.");
	}

	_SetStatusMessage(statusText.String());

	fInstallStatus = kReadyForInstall;
	fBeginButton->SetLabel(B_TRANSLATE("Begin"));
	fBeginButton->SetEnabled(srcItem && dstItem);

	// adjust "Write Boot Sector" and "Set up boot menu" buttons
	if (dstItem != NULL) {
		char buffer[256];
		snprintf(buffer, sizeof(buffer), B_TRANSLATE("Write boot sector to '%s'"),
			dstItem->Name());
		label = buffer;
	} else
		label = B_TRANSLATE("Write boot sector");
	fMakeBootableItem->SetEnabled(dstItem != NULL);
	fMakeBootableItem->SetLabel(label.String());
// TODO: Once bootman support writing to specific disks, enable this, since
// we would pass it the disk which contains the target partition.
//	fLaunchBootManagerItem->SetEnabled(dstItem != NULL);

	if (!fEncouragedToSetupPartitions && !foundOneSuitableTarget) {
		// Focus the users attention on the DriveSetup button
		fEncouragedToSetupPartitions = true;
		PostMessage(ENCOURAGE_DRIVESETUP);
	}
}


void
InstallerWindow::_PublishPackages()
{
	fPackagesView->Clean();
	PartitionMenuItem *item = (PartitionMenuItem *)fSrcMenu->FindMarked();
	if (item == NULL)
		return;

	BPath directory;
	BDiskDeviceRoster roster;
	BDiskDevice device;
	BPartition *partition;
	if (roster.GetPartitionWithID(item->ID(), &device, &partition) == B_OK) {
		if (partition->GetMountPoint(&directory) != B_OK)
			return;
	} else if (roster.GetDeviceWithID(item->ID(), &device) == B_OK) {
		if (device.GetMountPoint(&directory) != B_OK)
			return;
	} else
		return; // shouldn't happen

	directory.Append(kPackagesDirectoryPath);
	BDirectory dir(directory.Path());
	if (dir.InitCheck() != B_OK)
		return;

	BEntry packageEntry;
	BList packages;
	while (dir.GetNextEntry(&packageEntry) == B_OK) {
		Package* package = Package::PackageFromEntry(packageEntry);
		if (package != NULL)
			packages.AddItem(package);
	}
	packages.SortItems(_ComparePackages);

	fPackagesView->AddPackages(packages, new BMessage(PACKAGE_CHECKBOX));
	PostMessage(PACKAGE_CHECKBOX);
}


void
InstallerWindow::_SetStatusMessage(const char *text)
{
	fStatusView->SetText(text);
	fStatusView->InvalidateLayout();
		// In case the status message makes the text view higher than the
		// logo, then we need to resize te whole window to fit it.
}


void
InstallerWindow::_SetCopyEngineCancelSemaphore(sem_id id, bool alreadyLocked)
{
	if (fCopyEngineCancelSemaphore >= 0) {
		if (!alreadyLocked)
			acquire_sem(fCopyEngineCancelSemaphore);
		delete_sem(fCopyEngineCancelSemaphore);
	}
	fCopyEngineCancelSemaphore = id;
}


void
InstallerWindow::_QuitCopyEngine(bool askUser)
{
	if (fCopyEngineCancelSemaphore < 0)
		return;

	// First of all block the copy engine, so that it doesn't continue
	// while the alert is showing, which would be irritating.
	acquire_sem(fCopyEngineCancelSemaphore);

	bool quit = true;
	if (askUser) {
		BAlert* alert = new BAlert("cancel",
			B_TRANSLATE("Are you sure you want to to stop the installation?"),
			B_TRANSLATE_COMMENT("Continue", "In alert after pressing Stop"),
			B_TRANSLATE_COMMENT("Stop", "In alert after pressing Stop"), 0,
			B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->SetShortcut(1, B_ESCAPE);
		quit = alert->Go() != 0;
	}

	if (quit) {
		// Make it quit by having it's lock fail...
		_SetCopyEngineCancelSemaphore(-1, true);
	} else
		release_sem(fCopyEngineCancelSemaphore);
}


// #pragma mark -


int
InstallerWindow::_ComparePackages(const void* firstArg, const void* secondArg)
{
	const Group* group1 = *static_cast<const Group* const *>(firstArg);
	const Group* group2 = *static_cast<const Group* const *>(secondArg);
	const Package* package1 = dynamic_cast<const Package*>(group1);
	const Package* package2 = dynamic_cast<const Package*>(group2);
	int sameGroup = strcmp(group1->GroupName(), group2->GroupName());
	if (sameGroup != 0)
		return sameGroup;
	if (package2 == NULL)
		return -1;
	if (package1 == NULL)
		return 1;
	return strcmp(package1->Name(), package2->Name());
}


