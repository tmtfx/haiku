/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */


#include "GPUSettingsView.h"

#include <stdio.h>
#include <strings.h>

#include <Alert.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <Directory.h>
#include <File.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Screen.h>
#include <Slider.h>
#include <String.h>
#include <StringView.h>
#include <Window.h>

#include <edid.h>

#include "Constants.h"
#include "MonitorView.h"
#include "ScreenSettings.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "GPU Settings View"


GPUSettingsView::GPUSettingsView(const char* name, BWindow* window,
	int32 screenIndex, ScreenSettings* settings)
	:
	BView(name, 0),
	fWindow(window),
	fScreenIndex(screenIndex),
	fSettings(settings),
	fScreenMode(window, screenIndex),
	fHasSetCursorBitmap(false),
	fHas2DAcceleration(false),
	fDumpFilePanel(NULL),
	fLoadFilePanel(NULL)
{
	screen_id id;
	id.id = fScreenIndex;
	BScreen screen(id);

	BMessenger msgr("application/x-vnd.haiku-app_server");
	{
		BMessage request('gbdf');
		request.AddInt32("screen", fScreenIndex);
		BMessage reply;
		if (msgr.IsValid() && msgr.SendMessage(&request, &reply, 200000) == B_OK)
			reply.FindString("bdf", &fBdf);
	}

	if (!fBdf.IsEmpty())
		fScreenKey = fBdf;
	else
		fScreenKey.SetToFormat("idx%" B_PRId32, fScreenIndex);

	accelerant_device_info info;
	BString deviceLabel;
	if (fScreenMode.GetDeviceInfo(info) == B_OK
			&& (info.name[0] != '\0' || info.chipset[0] != '\0')) {
		// Same combining logic as ScreenWindow::_UpdateMonitor() for the
		// primary tab -- info.name alone is often just a generic driver
		// name ("ATI chipset"), the real card identification is in
		// info.chipset ("3D RAGE XL, PCI").
		if (info.name[0] != '\0' && info.chipset[0] != '\0')
			deviceLabel.SetToFormat("%s (%s)", info.name, info.chipset);
		else
			deviceLabel = info.name[0] != '\0' ? info.name : info.chipset;
	} else
		deviceLabel.SetToFormat(B_TRANSLATE("GPU %" B_PRId32), fScreenIndex);
	fDeviceInfo = new BStringView("device info", deviceLabel);
	fDeviceInfo->SetAlignment(B_ALIGN_CENTER);

	float scaling = std::max(1.0f, be_plain_font->Size() / 12.0f);
	fMonitorView = new MonitorView(BRect(0.0, 0.0, 80.0 * scaling,
			80.0 * scaling), "monitor", screen.Frame().IntegerWidth() + 1,
		screen.Frame().IntegerHeight() + 1, fScreenIndex);

	fDumpEDIDButton = new BButton("dumpEDID", B_TRANSLATE("Dump EDID"),
		new BMessage(BUTTON_DUMP_EDID_MSG));
	fLoadEDIDButton = new BButton("loadEDID", B_TRANSLATE("Load EDID"),
		new BMessage(BUTTON_LOAD_EDID_MSG));
	fUnsetEDIDButton = new BButton("unsetEDID", B_TRANSLATE("Unset EDID"),
		new BMessage(BUTTON_UNSET_EDID_MSG));

	bool supportsGetEDID = false, supportsSetEDID = false;
	{
		BMessage request('gedc');
		request.AddInt32("screen", fScreenIndex);
		BMessage reply;
		if (msgr.IsValid() && msgr.SendMessage(&request, &reply, 200000) == B_OK)
			reply.FindBool("supports_get_edid", &supportsGetEDID);

		BMessage request2('sedc');
		request2.AddInt32("screen", fScreenIndex);
		BMessage reply2;
		if (msgr.IsValid()
			&& msgr.SendMessage(&request2, &reply2, 200000) == B_OK) {
			reply2.FindBool("supports_set_edid", &supportsSetEDID);
		}
	}
	fDumpEDIDButton->SetEnabled(supportsGetEDID);
	fLoadEDIDButton->SetEnabled(supportsSetEDID);

	fBrightnessSlider = new BSlider("brightness", B_TRANSLATE("Brightness:"),
		new BMessage(SLIDER_BRIGHTNESS_MSG), 0, 255, B_HORIZONTAL);
	float brightness;
	if (screen.GetBrightness(&brightness) == B_OK)
		fBrightnessSlider->SetValue(brightness * 255);
	else
		fBrightnessSlider->Hide();

	_BuildResolutionMenu();

	{
		BMessage request('hsbm');
		request.AddInt32("screen", fScreenIndex);
		BMessage reply;
		if (msgr.IsValid() && msgr.SendMessage(&request, &reply, 200000) == B_OK)
			reply.FindBool("has_set_cursor_bitmap", &fHasSetCursorBitmap);

		BMessage request2('h2da');
		request2.AddInt32("screen", fScreenIndex);
		BMessage reply2;
		if (msgr.IsValid()
			&& msgr.SendMessage(&request2, &reply2, 200000) == B_OK) {
			reply2.FindBool("has_2d_acceleration", &fHas2DAcceleration);
		}
	}

	fHardwareCursorCheckbox = new BCheckBox("hardware_cursor",
		B_TRANSLATE("Enable hardware cursor bitmap"),
		new BMessage(TOGGLE_HARDWARE_CURSOR_MSG));
	fHardwareCursorCheckbox->SetValue(fHasSetCursorBitmap
			&& fSettings->HardwareCursorEnabled(fScreenKey)
		? B_CONTROL_ON : B_CONTROL_OFF);

	f2DAccelerationCheckbox = new BCheckBox("2d_acceleration",
		B_TRANSLATE("Enable 2D hardware acceleration"),
		new BMessage(TOGGLE_2D_ACCELERATION_MSG));
	f2DAccelerationCheckbox->SetValue(fHas2DAcceleration
			&& fSettings->Acceleration2DEnabled(fScreenKey)
		? B_CONTROL_ON : B_CONTROL_OFF);

	f2DDebugCheckbox = new BCheckBox("2d_debug",
		B_TRANSLATE("Enable 2D debug indicator"),
		new BMessage(TOGGLE_2D_DEBUG_MSG));
	f2DDebugCheckbox->SetValue(fHas2DAcceleration
			&& f2DAccelerationCheckbox->Value() == B_CONTROL_ON
			&& fSettings->Visual2DDebugEnabled(fScreenKey)
		? B_CONTROL_ON : B_CONTROL_OFF);

	fScreenBox = new BBox("screen box");
	BGroupView* screenGroup = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	fScreenBox->AddChild(screenGroup);
	screenGroup->GroupLayout()->SetInsets(B_USE_DEFAULT_SPACING,
		B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING);
	screenGroup->AddChild(fDeviceInfo);
	screenGroup->AddChild(fMonitorView);
	screenGroup->AddChild(BLayoutBuilder::Group<>(B_HORIZONTAL,
			B_USE_SMALL_SPACING)
		.Add(fDumpEDIDButton)
		.Add(fLoadEDIDButton)
		.Add(fUnsetEDIDButton)
		.View());
	screenGroup->AddChild(fBrightnessSlider);

	BLayoutBuilder::Group<>(this, B_HORIZONTAL, B_USE_DEFAULT_SPACING)
		.AddGroup(B_VERTICAL, 0, 1)
			.Add(fScreenBox)
			.AddGlue()
			.End()
		.AddGroup(B_VERTICAL, B_USE_DEFAULT_SPACING, 1)
			.Add(fResolutionField)
			.Add(fHardwareCursorCheckbox)
			.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
				.Add(f2DAccelerationCheckbox)
				.Add(f2DDebugCheckbox)
				.AddGlue()
				.End()
			.AddGlue()
			.End()
		.SetInsets(B_USE_WINDOW_SPACING);

	_RefreshEdidGatedControls();
}


/*!	Whether this screen effectively has a usable EDID right now (driver
	reports one AND the user hasn't forced it off via "Unset EDID") --
	see AppServer.cpp's 'hedi' handler / MULTIGPU_PLAN.md EDID section.
*/
bool
GPUSettingsView::_QueryHasEDID() const
{
	bool hasEDID = false;
	BMessenger msgr("application/x-vnd.haiku-app_server");
	BMessage request('hedi');
	request.AddInt32("screen", fScreenIndex);
	BMessage reply;
	if (msgr.IsValid() && msgr.SendMessage(&request, &reply, 200000) == B_OK)
		reply.FindBool("has_edid", &hasEDID);
	return hasEDID;
}


/*!	Without an EDID, the driver has no idea what modes/cursor/2D behavior
	are actually safe for whatever (if anything) is connected, so every
	control that depends on that is disabled until one is assigned (via
	"Load EDID" here, or a driver .settings override) -- see
	MULTIGPU_PLAN.md EDID section. Capability (fHasSetCursorBitmap etc.)
	and EDID-presence are independent gates, both must hold.
*/
void
GPUSettingsView::_RefreshEdidGatedControls()
{
	bool hasEDID = _QueryHasEDID();

	fResolutionField->SetEnabled(hasEDID);
	fBrightnessSlider->SetEnabled(hasEDID);

	fHardwareCursorCheckbox->SetEnabled(hasEDID && fHasSetCursorBitmap);
	f2DAccelerationCheckbox->SetEnabled(hasEDID && fHas2DAcceleration);
	f2DDebugCheckbox->SetEnabled(hasEDID && fHas2DAcceleration
		&& f2DAccelerationCheckbox->Value() == B_CONTROL_ON);

	fUnsetEDIDButton->SetEnabled(hasEDID);

	_UpdateMonitor();
}


/*!	Sets fScreenBox's label to the monitor name decoded from EDID (vendor
	+ name + diagonal size), same as ScreenWindow::_UpdateMonitor() does
	for the primary tab's fScreenBox -- without this, a monitor's actual
	name never appears here even after a successful EDID load, unlike on
	the primary tab. Falls back to a generic label when there's no EDID
	(or no monitor section in it) to decode yet.
*/
void
GPUSettingsView::_UpdateMonitor()
{
	monitor_info info;
	float diagonalInches;
	if (fScreenMode.GetMonitorInfo(info, &diagonalInches) == B_OK) {
		char text[512];
		snprintf(text, sizeof(text), "%s%s%s %g\"", info.vendor,
			info.name[0] ? " " : "", info.name, diagonalInches);
		fScreenBox->SetLabel(text);
	} else {
		fScreenBox->SetLabel(B_TRANSLATE("Display info"));
	}
}


GPUSettingsView::~GPUSettingsView()
{
	delete fDumpFilePanel;
	delete fLoadFilePanel;
}


void
GPUSettingsView::_BuildResolutionMenu()
{
	fResolutionMenu = new BPopUpMenu("resolution", true, true);
	_PopulateResolutionMenu();

	fResolutionField = new BMenuField("ResolutionMenu",
		B_TRANSLATE("Resolution:"), fResolutionMenu);
	fResolutionField->SetAlignment(B_ALIGN_RIGHT);
}


void
GPUSettingsView::_PopulateResolutionMenu()
{
	int32 fullModeCount = fScreenMode.CountModes();
	uint16 previousWidth = 0, previousHeight = 0;
	screen_mode current;
	fScreenMode.Get(current);

	BMenuItem* selected = NULL;
	for (int32 i = 0; i < fullModeCount; i++) {
		screen_mode mode = fScreenMode.ModeAt(i);
		if (mode.width == previousWidth && mode.height == previousHeight)
			continue;
		previousWidth = mode.width;
		previousHeight = mode.height;

		BMessage* message = new BMessage(POP_RESOLUTION_MSG);
		message->AddInt32("width", mode.width);
		message->AddInt32("height", mode.height);

		BString name;
		name.SetToFormat(B_TRANSLATE_COMMENT("%" B_PRId32" × %" B_PRId32,
			"The '×' is the Unicode multiplication sign U+00D7"),
			mode.width, mode.height);

		BMenuItem* item = new BMenuItem(name.String(), message);
		fResolutionMenu->AddItem(item);
		if (mode.width == current.width && mode.height == current.height)
			selected = item;
	}

	if (selected != NULL)
		selected->SetMarked(true);
}


void
GPUSettingsView::_RefreshResolutionMenu()
{
	while (fResolutionMenu->CountItems() > 0)
		delete fResolutionMenu->RemoveItem((int32)0);

	fScreenMode.UpdateModeList();
	_PopulateResolutionMenu();

	for (int32 i = 0; i < fResolutionMenu->CountItems(); i++)
		fResolutionMenu->ItemAt(i)->SetTarget(this);
}


void
GPUSettingsView::AttachedToWindow()
{
	fDumpEDIDButton->SetTarget(this);
	fLoadEDIDButton->SetTarget(this);
	fUnsetEDIDButton->SetTarget(this);
	fBrightnessSlider->SetTarget(this);
	fHardwareCursorCheckbox->SetTarget(this);
	f2DAccelerationCheckbox->SetTarget(this);
	f2DDebugCheckbox->SetTarget(this);

	for (int32 i = 0; i < fResolutionMenu->CountItems(); i++)
		fResolutionMenu->ItemAt(i)->SetTarget(this);
}


void
GPUSettingsView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case POP_RESOLUTION_MSG:
		{
			int32 width, height;
			if (message->FindInt32("width", &width) == B_OK
				&& message->FindInt32("height", &height) == B_OK) {
				screen_mode mode;
				fScreenMode.Get(mode);
				mode.width = width;
				mode.height = height;
				status_t status = fScreenMode.Set(mode);
				if (status != B_OK) {
					BAlert* alert = new BAlert(B_TRANSLATE("Error"),
						strerror(status), B_TRANSLATE("OK"), NULL, NULL,
						B_WIDTH_AS_USUAL, B_STOP_ALERT);
					alert->Go();
				}
			}
			break;
		}

		case SLIDER_BRIGHTNESS_MSG:
		{
			screen_id id;
			id.id = fScreenIndex;
			BScreen screen(id);
			screen.SetBrightness(message->FindInt32("be:value") / 255.f);
			break;
		}

		case TOGGLE_HARDWARE_CURSOR_MSG:
		{
			bool enabled = fHardwareCursorCheckbox->Value() == B_CONTROL_ON;
			fSettings->SetHardwareCursorEnabled(fScreenKey, fScreenIndex,
				enabled);
			break;
		}

		case TOGGLE_2D_ACCELERATION_MSG:
		{
			bool enabled = f2DAccelerationCheckbox->Value() == B_CONTROL_ON;
			fSettings->SetAcceleration2DEnabled(fScreenKey, fScreenIndex,
				enabled);
			if (enabled) {
				f2DDebugCheckbox->SetEnabled(_QueryHasEDID());
			} else {
				f2DDebugCheckbox->SetValue(B_CONTROL_OFF);
				f2DDebugCheckbox->SetEnabled(false);
				fSettings->SetVisual2DDebugEnabled(fScreenKey, fScreenIndex,
					false);
			}
			break;
		}

		case TOGGLE_2D_DEBUG_MSG:
		{
			bool enabled = f2DDebugCheckbox->Value() == B_CONTROL_ON;
			fSettings->SetVisual2DDebugEnabled(fScreenKey, fScreenIndex,
				enabled);
			break;
		}

		case BUTTON_DUMP_EDID_MSG:
			if (fDumpFilePanel == NULL) {
				BMessenger messenger(this);
				fDumpFilePanel = new BFilePanel(B_SAVE_PANEL, &messenger,
					NULL, B_FILE_NODE, false, NULL);
				fDumpFilePanel->SetSaveText("edid_info.bin");
			}
			fDumpFilePanel->Show();
			break;

		case BUTTON_LOAD_EDID_MSG:
			if (fLoadFilePanel == NULL) {
				BMessenger messenger(this);
				fLoadFilePanel = new BFilePanel(B_OPEN_PANEL, &messenger,
					NULL, B_FILE_NODE, false, NULL);
			}
			fLoadFilePanel->Show();
			break;

		case BUTTON_UNSET_EDID_MSG:
			fSettings->SetEdidUnset(fBdf, true);
			_RefreshEdidGatedControls();
			break;

		case B_SAVE_REQUESTED:
		{
			entry_ref dirRef;
			const char* name;
			if (message->FindRef("directory", &dirRef) == B_OK
				&& message->FindString("name", &name) == B_OK) {
				_DumpEDID(&dirRef, name);
			}
			break;
		}

		case B_REFS_RECEIVED:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK)
				_LoadEDID(&ref);
			break;
		}

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
GPUSettingsView::_DumpEDID(const entry_ref* dirRef, const char* name)
{
	BMessenger msgr("application/x-vnd.haiku-app_server");
	BMessage request('gedi');
	request.AddInt32("screen", fScreenIndex);
	BMessage reply;
	if (!msgr.IsValid() || msgr.SendMessage(&request, &reply, 200000) != B_OK) {
		BAlert* alert = new BAlert(B_TRANSLATE("Error"),
			B_TRANSLATE("Could not communicate with the app_server."),
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
		return;
	}

	status_t status = B_ERROR;
	reply.FindInt32("status", &status);
	if (status != B_OK) {
		BString errStr(B_TRANSLATE("Could not retrieve EDID from display driver:\n\t"));
		errStr << strerror(status);
		BAlert* alert = new BAlert(B_TRANSLATE("Error"), errStr.String(),
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
		return;
	}

	const void* edidData = NULL;
	ssize_t dataSize = 0;
	if (reply.FindData("edid", B_RAW_TYPE, &edidData, &dataSize) != B_OK
		|| edidData == NULL || dataSize <= 0) {
		BAlert* alert = new BAlert(B_TRANSLATE("Error"),
			B_TRANSLATE("Invalid EDID data received from app_server."),
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
		return;
	}

	BDirectory directory(dirRef);
	BFile file(&directory, name, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK) {
		BString errStr(B_TRANSLATE("Could not create file:\n\t"));
		errStr << strerror(file.InitCheck());
		BAlert* alert = new BAlert(B_TRANSLATE("Error"), errStr.String(),
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
		return;
	}

	ssize_t written = file.Write(edidData, dataSize);
	if (written < dataSize) {
		BAlert* alert = new BAlert(B_TRANSLATE("Error"),
			B_TRANSLATE("Failed to write complete EDID data to file."),
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
		return;
	}

	BAlert* alert = new BAlert(B_TRANSLATE("Dump EDID"),
		B_TRANSLATE("EDID successfully saved to disk."),
		B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_INFO_ALERT);
	alert->Go();
}


void
GPUSettingsView::_LoadEDID(const entry_ref* ref)
{
	BFile file(ref, B_READ_ONLY);
	if (file.InitCheck() != B_OK) {
		BString errStr(B_TRANSLATE("Could not open file:\n\t"));
		errStr << strerror(file.InitCheck());
		BAlert* alert = new BAlert(B_TRANSLATE("Error"), errStr.String(),
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
		return;
	}

	off_t size = 0;
	file.GetSize(&size);
	if (size < (off_t)sizeof(edid1_info)) {
		BAlert* alert = new BAlert(B_TRANSLATE("Error"),
			B_TRANSLATE("File is too small to contain valid edid1_info structure."),
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
		return;
	}

	edid1_info edid;
	if (file.Read(&edid, sizeof(edid)) != (ssize_t)sizeof(edid)) {
		BAlert* alert = new BAlert(B_TRANSLATE("Error"),
			B_TRANSLATE("Error reading EDID data from file."),
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
		return;
	}

	BMessenger msgr("application/x-vnd.haiku-app_server");
	BMessage request('sedi');
	request.AddData("edid", B_RAW_TYPE, &edid, sizeof(edid));
	request.AddInt32("version", 1);
	request.AddInt32("screen", fScreenIndex);
	BMessage reply;
	if (!msgr.IsValid() || msgr.SendMessage(&request, &reply, 200000) != B_OK) {
		BAlert* alert = new BAlert(B_TRANSLATE("Error"),
			B_TRANSLATE("Could not communicate with the app_server."),
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
		return;
	}

	status_t status = B_ERROR;
	reply.FindInt32("status", &status);
	if (status != B_OK) {
		BString errStr(B_TRANSLATE("Could not set EDID on display driver:\n\t"));
		errStr << strerror(status);
		BAlert* alert = new BAlert(B_TRANSLATE("Error"), errStr.String(),
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
		return;
	}

	// Loading a real EDID is an explicit reassignment -- any prior
	// "Unset EDID" override no longer applies.
	fSettings->SetEdidUnset(fBdf, false);

	_RefreshResolutionMenu();
	_RefreshEdidGatedControls();
	_OfferToSaveStableEdid(edid);
		// its own BAlert covers "EDID successfully loaded", no need for
		// a second one here
}


/*!	Load EDID is always session-only (the driver's set_edid_info hook
	only ever touches its own in-memory shared_info -- see
	MULTIGPU_PLAN.md EDID section). Offers to also save \a edid to a
	fixed, predictable location and tells the user the exact ".settings"
	line to add -- but does NOT edit any driver's .settings file
	automatically: driver settings filenames aren't consistent enough to
	guess safely (some are "<module>", some "<module>.settings"), and
	silently writing to the wrong one would fail without any visible
	error.
*/
void
GPUSettingsView::_OfferToSaveStableEdid(const edid1_info& edid)
{
	BAlert* offer = new BAlert(B_TRANSLATE("Load EDID"),
		B_TRANSLATE("EDID successfully loaded for this session. "
			"Make it permanent (load automatically on every boot)?"),
		B_TRANSLATE("Just this session"), B_TRANSLATE("Make permanent"),
		NULL, B_WIDTH_AS_USUAL, B_IDEA_ALERT);
	if (offer->Go() != 1)
		return;

	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;
	path.Append("edid");
	create_directory(path.Path(), 0755);

	BString fileName(fBdf.IsEmpty() ? "default" : fBdf.String());
	fileName << ".bin";
	path.Append(fileName.String());

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK
		|| file.Write(&edid, sizeof(edid)) != (ssize_t)sizeof(edid)) {
		BAlert* errAlert = new BAlert(B_TRANSLATE("Error"),
			B_TRANSLATE("Could not save the EDID file."),
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		errAlert->Go();
		return;
	}

	BString message(B_TRANSLATE(
		"Saved to:\n\t%path%\n\n"
		"To load it automatically on every boot, add this line to this "
		"card's driver .settings file, under:\n\t"
		"~/config/settings/kernel/drivers/\n"
		"(named after the driver module -- conventions vary, e.g. "
		"\"intel_extreme\" or \"radeon_hd.settings\"):\n\n"
		"\tedid %path%"));
	message.ReplaceAll("%path%", path.Path());

	BAlert* doneAlert = new BAlert(B_TRANSLATE("Load EDID"),
		message.String(), B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
		B_INFO_ALERT);
	doneAlert->Go();
}
