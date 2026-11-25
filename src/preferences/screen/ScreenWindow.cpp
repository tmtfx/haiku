/*
 * Copyright 2001-2015 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus, superstippi@gmx.de
 *		Andrew Bachmann
 *		Stefano Ceccherini, burton666@libero.it
 *		Alexandre Deckner, alex@zappotek.com
 *		Axel Dörfler, axeld@pinc-software.de
 *		Rene Gollent, rene@gollent.com
 *		Thomas Kurschel
 *		Rafael Romo
 *		John Scipione, jscipione@gmail.com
 */


#include "ScreenWindow.h"

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include <Alert.h>
#include <Application.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Screen.h>
#include <SpaceLayoutItem.h>
#include <Spinner.h>
#include <String.h>
#include <StringList.h>
#include <StringView.h>
#include <TabView.h>
#include <Window.h>

#include <InterfacePrivate.h>
#include <CheckBox.h>
#include <FilePanel.h>
#include <ListItem.h>
#include <ListView.h>
#include <ScrollView.h>
#include <edid.h>

#include "AlertWindow.h"
#include "Constants.h"
#include "RefreshWindow.h"
#include "GPUSettingsView.h"
#include "MonitorView.h"
#include "ScreenSettings.h"
#include "Utility.h"
#include "WorkspaceGridView.h"

/* Note, this headers defines a *private* interface to the Radeon accelerant.
 * It's a solution that works with the current BeOS interface that Haiku
 * adopted.
 * However, it's not a nice and clean solution. Don't use this header in any
 * application if you can avoid it. No other driver is using this, or should
 * be using this.
 * It will be replaced as soon as we introduce an updated accelerant interface
 * which may even happen before R1 hits the streets.
 */
#include "multimon.h"	// the usual: DANGER WILL, ROBINSON!


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Screen"


const char* kBackgroundsSignature = "application/x-vnd.Haiku-Backgrounds";

// list of officially supported colour spaces
static const struct {
	color_space	space;
	const char*	label;
} kColorSpaces[] = {
	{ B_CMAP8, B_TRANSLATE("8 bits/pixel, 256 colors") },
	{ B_RGB15, B_TRANSLATE("15 bits/pixel, 32768 colors") },
	{ B_RGB16, B_TRANSLATE("16 bits/pixel, 65536 colors") },
	{ B_RGB24, B_TRANSLATE("24 bits/pixel, 16 million colors") },
	{ B_RGB32, B_TRANSLATE("24 bits/pixel, 16 million colors") },
	{ B_RGB30, B_TRANSLATE("30 bits/pixel, 1 billion colors") }
};
static const int32 kColorSpaceCount = B_COUNT_OF(kColorSpaces);

// list of standard refresh rates
static const int32 kRefreshRates[] = { 60, 70, 72, 75, 80, 85, 95, 100 };
static const int32 kRefreshRateCount = B_COUNT_OF(kRefreshRates);

// list of combine modes
static const struct {
	combine_mode	mode;
	const char		*name;
} kCombineModes[] = {
	{ kCombineDisable, B_TRANSLATE("disable") },
	{ kCombineHorizontally, B_TRANSLATE("horizontally") },
	{ kCombineVertically, B_TRANSLATE("vertically") }
};
static const int32 kCombineModeCount = B_COUNT_OF(kCombineModes);


static BString
tv_standard_to_string(uint32 mode)
{
	switch (mode) {
		case 0:		return "disabled";
		case 1:		return "NTSC";
		case 2:		return "NTSC Japan";
		case 3:		return "PAL BDGHI";
		case 4:		return "PAL M";
		case 5:		return "PAL N";
		case 6:		return "SECAM";
		case 101:	return "NTSC 443";
		case 102:	return "PAL 60";
		case 103:	return "PAL NC";
		default:
		{
			BString name;
			name << "??? (" << mode << ")";

			return name;
		}
	}
}


static void
resolution_to_string(screen_mode& mode, BString &string)
{
	string.SetToFormat(B_TRANSLATE_COMMENT("%" B_PRId32" × %" B_PRId32,
			"The '×' is the Unicode multiplication sign U+00D7"),
			mode.width, mode.height);
}


static void
refresh_rate_to_string(float refresh, BString &string,
	bool appendUnit = true, bool alwaysWithFraction = false)
{
	snprintf(string.LockBuffer(32), 32, "%.*g", refresh >= 100.0 ? 4 : 3,
		refresh);
	string.UnlockBuffer();

	if (appendUnit)
		string << " " << B_TRANSLATE("Hz");
}


// Multi-GPU (MULTIGPU_PLAN.md Fase F): human-recognizable name for a
// screen's card, used as its tab label in the preflet.
static BString
gpu_device_name(BMessenger& appServerMessenger, int32 index)
{
	BMessage request('devn');
	request.AddInt32("screen", index);
	BMessage reply;
	BString name;
	if (appServerMessenger.IsValid()
		&& appServerMessenger.SendMessage(&request, &reply, 200000) == B_OK) {
		reply.FindString("name", &name);
	}
	if (name.IsEmpty())
		name.SetToFormat(B_TRANSLATE("GPU %" B_PRId32), index);
	return name;
}


// Multi-GPU: which gScreenManager::ScreenAt() index is the effective
// primary (configured GPU order's first entry if it matches a
// currently discovered screen, else whichever screen was discovered
// first -- see AppServer.cpp's is_primary_screen(), which this mirrors
// via the 'prim' query so the preflet always gives ITS full-featured
// tab to the same screen Desktop treats as primary, instead of
// hardcoding screen index 0 regardless of configured order).
static int32
primary_screen_index(BMessenger& appServerMessenger)
{
	BMessage request('prim');
	BMessage reply;
	int32 index = 0;
	if (appServerMessenger.IsValid()
		&& appServerMessenger.SendMessage(&request, &reply, 200000) == B_OK) {
		reply.FindInt32("index", &index);
	}
	return index;
}


// Multi-GPU: the key ScreenSettings' per-GPU preferences persist under
// for a screen -- its PCI BDF (queried via 'gbdf') if resolvable, else
// a fallback "idx<N>" that at least works within a single boot session
// (cards with no resolvable PCI location, e.g. virtio, have no better
// stable identity available -- see MULTIGPU_PLAN.md EDID section).
// Duplicated near-identically in GPUSettingsView.cpp and
// ScreenManager.cpp's apply_user_graphics_settings(), same convention.
static BString
gpu_settings_key(BMessenger& appServerMessenger, int32 screenIndex)
{
	BMessage request('gbdf');
	request.AddInt32("screen", screenIndex);
	BMessage reply;
	BString bdf;
	if (appServerMessenger.IsValid()
		&& appServerMessenger.SendMessage(&request, &reply, 200000) == B_OK) {
		reply.FindString("bdf", &bdf);
	}

	if (!bdf.IsEmpty())
		return bdf;

	BString key("idx");
	key << screenIndex;
	return key;
}


static const char*
screen_errors(status_t status)
{
	switch (status) {
		case B_ENTRY_NOT_FOUND:
			return B_TRANSLATE("Unknown mode");
		// TODO: add more?

		default:
			return strerror(status);
	}
}


//	#pragma mark - ScreenWindow


ScreenWindow::ScreenWindow(ScreenSettings* settings)
	:
	BWindow(settings->WindowFrame(), B_TRANSLATE_SYSTEM_NAME("Screen"),
		B_TITLED_WINDOW, B_NOT_RESIZABLE | B_NOT_ZOOMABLE
			| B_AUTO_UPDATE_SIZE_LIMITS, B_ALL_WORKSPACES),
	fScreenIndex(0),
	fIsVesa(false),
	fBootWorkspaceApplied(false),
		fHasCursorAnimationCapability(false),
	fUserSelectedColorSpace(NULL),
	fOtherRefresh(NULL),
	fScreenMode(this),
	fUndoScreenMode(this),
	fModified(false),
	fDumpFilePanel(NULL),
	fLoadFilePanel(NULL)
{
	BScreen screen(this);

	{
		BMessenger appServerMessenger("application/x-vnd.haiku-app_server");
		// This window's own full-featured tab always targets the
		// effective primary GPU, not gScreenManager index 0 -- those
		// only coincide when no GPU order has been configured yet, or
		// when the primary happens to also be the one discovered first
		// (see primary_screen_index() and MULTIGPU_PLAN.md Fase F).
		fScreenIndex = primary_screen_index(appServerMessenger);
		fScreenKey = gpu_settings_key(appServerMessenger, fScreenIndex);
	}

	accelerant_device_info info;
	if (screen.GetDeviceInfo(&info) == B_OK
		&& !strcasecmp(info.chipset, "VESA"))
		fIsVesa = true;

	_UpdateOriginal();
	_BuildSupportedColorSpaces();
	fActive = fSelected = fOriginal;

	fSettings = settings;

	// we need the "Current Workspace" first to get its height

	BPopUpMenu* popUpMenu = new BPopUpMenu(B_TRANSLATE("Current workspace"),
		true, true);
	fAllWorkspacesItem = new BMenuItem(B_TRANSLATE("All workspaces"),
		new BMessage(WORKSPACE_CHECK_MSG));
	popUpMenu->AddItem(fAllWorkspacesItem);
	BMenuItem *item = new BMenuItem(B_TRANSLATE("Current workspace"),
		new BMessage(WORKSPACE_CHECK_MSG));

	popUpMenu->AddItem(item);
	fAllWorkspacesItem->SetMarked(true);

	BMenuField* workspaceMenuField = new BMenuField("WorkspaceMenu", NULL,
		popUpMenu);
	workspaceMenuField->ResizeToPreferred();

	// box on the left with workspace count and monitor view

	fScreenBox = new BBox("screen box");
	BGroupView* groupView = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	fScreenBox->AddChild(groupView);
	fScreenBox->SetLabel("placeholder");
		// Needed for layouting, will be replaced with screen name/size
	groupView->GroupLayout()->SetInsets(B_USE_DEFAULT_SPACING,
		B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING);

	fDeviceInfo = new BStringView("device info", "");
	fDeviceInfo->SetAlignment(B_ALIGN_CENTER);
	groupView->AddChild(fDeviceInfo);

	float scaling = std::max(1.0f, be_plain_font->Size() / 12.0f);
	fMonitorView = new MonitorView(BRect(0.0, 0.0, 80.0 * scaling,
			80.0 * scaling), "monitor", screen.Frame().IntegerWidth() + 1,
		screen.Frame().IntegerHeight() + 1);
	fMonitorView->SetToolTip(B_TRANSLATE("Set background" B_UTF8_ELLIPSIS));
	groupView->AddChild(fMonitorView);

	// EDID dump and load buttons
	fDumpEDIDButton = new BButton("dumpEDID", B_TRANSLATE("Dump EDID"),
		new BMessage(BUTTON_DUMP_EDID_MSG));
	fLoadEDIDButton = new BButton("loadEDID", B_TRANSLATE("Load EDID"),
		new BMessage(BUTTON_LOAD_EDID_MSG));

	// Only enable each button if the accelerant actually exposes the
	// corresponding hook (B_GET_EDID_INFO / B_SET_EDID_INFO).
	bool supportsGetEDID = false;
	{
		BMessenger msgr("application/x-vnd.haiku-app_server");
		BMessage request('gedc');
		request.AddInt32("screen", fScreenIndex);
		BMessage reply;
		if (msgr.IsValid() && msgr.SendMessage(&request, &reply, 200000) == B_OK)
			reply.FindBool("supports_get_edid", &supportsGetEDID);
	}
	fDumpEDIDButton->SetEnabled(supportsGetEDID);

	bool supportsSetEDID = false;
	{
		BMessenger msgr("application/x-vnd.haiku-app_server");
		BMessage request('sedc');
		request.AddInt32("screen", fScreenIndex);
		BMessage reply;
		if (msgr.IsValid() && msgr.SendMessage(&request, &reply, 200000) == B_OK)
			reply.FindBool("supports_set_edid", &supportsSetEDID);
	}
	fLoadEDIDButton->SetEnabled(supportsSetEDID);

	groupView->AddChild(BLayoutBuilder::Group<>(B_HORIZONTAL, B_USE_SMALL_SPACING)
		.Add(fDumpEDIDButton)
		.Add(fLoadEDIDButton)
		.View());

	// brightness slider
	fBrightnessSlider = new BSlider("brightness", B_TRANSLATE("Brightness:"),
		NULL, 0, 255, B_HORIZONTAL);
	groupView->AddChild(fBrightnessSlider);

	if (screen.GetBrightness(&fOriginalBrightness) == B_OK) {
		fBrightnessSlider->SetModificationMessage(
			new BMessage(SLIDER_BRIGHTNESS_MSG));
		fBrightnessSlider->SetValue(fOriginalBrightness * 255);
	} else {
		// The driver does not support changing the brightness,
		// so hide the slider
		fBrightnessSlider->Hide();
		fOriginalBrightness = -1;
	}

	// box on the left below the screen box with workspaces

	BBox* workspacesBox = new BBox("workspaces box");
	workspacesBox->SetLabel(B_TRANSLATE("Workspaces"));

	BGroupLayout* workspacesLayout = new BGroupLayout(B_VERTICAL);
	workspacesLayout->SetInsets(B_USE_DEFAULT_SPACING,
		be_control_look->DefaultItemSpacing() * 2, B_USE_DEFAULT_SPACING,
		B_USE_DEFAULT_SPACING);
	workspacesBox->SetLayout(workspacesLayout);

	fColumnsControl = new BSpinner("columns", B_TRANSLATE("Columns:"),
		new BMessage(kMsgWorkspaceColumnsChanged));
	fColumnsControl->SetAlignment(B_ALIGN_RIGHT);
	fColumnsControl->SetRange(1, 32);

	fRowsControl = new BSpinner("rows", B_TRANSLATE("Rows:"),
		new BMessage(kMsgWorkspaceRowsChanged));
	fRowsControl->SetAlignment(B_ALIGN_RIGHT);
	fRowsControl->SetRange(1, 32);

	uint32 columns;
	uint32 rows;
	BPrivate::get_workspaces_layout(&columns, &rows);
	fColumnsControl->SetValue(columns);
	fRowsControl->SetValue(rows);

	workspacesBox->AddChild(BLayoutBuilder::Group<>()
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
			.AddGroup(B_HORIZONTAL, 0)
				.AddGlue()
				.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
					// columns
					.Add(fColumnsControl->CreateLabelLayoutItem(), 0, 0)
					.Add(fColumnsControl->CreateTextViewLayoutItem(), 1, 0)
					// rows
					.Add(fRowsControl->CreateLabelLayoutItem(), 0, 1)
					.Add(fRowsControl->CreateTextViewLayoutItem(), 1, 1)
					.End()
				.AddGlue()
				.End()
			.End()
		.View());

	// box on the right with screen resolution, etc.

	BBox* controlsBox = new BBox("controls box");
	controlsBox->SetLabel(workspaceMenuField);
	BGroupView* outerControlsView = new BGroupView(B_VERTICAL);
	outerControlsView->GroupLayout()->SetInsets(B_USE_DEFAULT_SPACING,
		B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING);
	controlsBox->AddChild(outerControlsView);

	menu_layout layout = B_ITEMS_IN_COLUMN;

	// There are modes in the list with the same resolution but different bpp or refresh rates.
	// We don't want to take these into account when computing the menu layout, so we need to
	// count how many entries we will really have in the menu.
	int fullModeCount = fScreenMode.CountModes();
	int modeCount = 0;
	int index = 0;
	uint16 maxWidth = 0;
	uint16 maxHeight = 0;
	uint16 previousWidth = 0;
	uint16 previousHeight = 0;
	for (int32 i = 0; i < fullModeCount; i++) {
		screen_mode mode = fScreenMode.ModeAt(i);

		if (mode.width == previousWidth && mode.height == previousHeight)
			continue;
		modeCount++;
		previousWidth = mode.width;
		previousHeight = mode.height;
		if (maxWidth < mode.width)
			maxWidth = mode.width;
		if (maxHeight < mode.height)
			maxHeight = mode.height;
	}

	if (modeCount > 16)
		layout = B_ITEMS_IN_MATRIX;

	fResolutionMenu = new BPopUpMenu("resolution", true, true, layout);

	// Compute the size we should allocate to each item in the menu
	BRect itemRect;
	if (layout == B_ITEMS_IN_MATRIX) {
		BFont menuFont;
		font_height fontHeight;

		fResolutionMenu->GetFont(&menuFont);
		menuFont.GetHeight(&fontHeight);
		itemRect.left = itemRect.top = 0;
		itemRect.bottom = fontHeight.ascent + fontHeight.descent + 4;
		itemRect.right = menuFont.StringWidth("99999x99999") + 16;
		rows = modeCount / 3 + 1;
	}

	index = 0;
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

		if (layout == B_ITEMS_IN_COLUMN)
			fResolutionMenu->AddItem(new BMenuItem(name.String(), message));
		else {
			int y = index % rows;
			int x = index / rows;
			itemRect.OffsetTo(x * itemRect.Width(), y * itemRect.Height());
			fResolutionMenu->AddItem(new BMenuItem(name.String(), message), itemRect);
		}

		index++;
	}

	fMonitorView->SetMaxResolution(maxWidth, maxHeight);

	fResolutionField = new BMenuField("ResolutionMenu",
		B_TRANSLATE("Resolution:"), fResolutionMenu);
	fResolutionField->SetAlignment(B_ALIGN_RIGHT);

	fColorsMenu = new BPopUpMenu("colors", true, false);

	for (int32 i = 0; i < kColorSpaceCount; i++) {
		if ((fSupportedColorSpaces & (1 << i)) == 0)
			continue;

		BMessage* message = new BMessage(POP_COLORS_MSG);
		message->AddInt32("space", kColorSpaces[i].space);

		BMenuItem* item = new BMenuItem(kColorSpaces[i].label, message);
		if (kColorSpaces[i].space == screen.ColorSpace())
			fUserSelectedColorSpace = item;

		fColorsMenu->AddItem(item);
	}

	fColorsField = new BMenuField("ColorsMenu", B_TRANSLATE("Colors:"),
		fColorsMenu);
	fColorsField->SetAlignment(B_ALIGN_RIGHT);

	fRefreshMenu = new BPopUpMenu("refresh rate", true, true);

	float min, max;
	if (fScreenMode.GetRefreshLimits(fActive, min, max) != B_OK) {
		// if we couldn't obtain the refresh limits, reset to the default
		// range. Constraints from detected monitors will fine-tune this
		// later.
		min = kRefreshRates[0];
		max = kRefreshRates[kRefreshRateCount - 1];
	}

	if (min == max) {
		// This is a special case for drivers that only support a single
		// frequency, like the VESA driver
		BString name;
		refresh_rate_to_string(min, name);
		BMessage *message = new BMessage(POP_REFRESH_MSG);
		message->AddFloat("refresh", min);
		BMenuItem *item = new BMenuItem(name.String(), message);
		fRefreshMenu->AddItem(item);
		item->SetEnabled(false);
	} else {
		monitor_info info;
		if (fScreenMode.GetMonitorInfo(info) == B_OK) {
			min = max_c(info.min_vertical_frequency, min);
			max = min_c(info.max_vertical_frequency, max);
		}

		for (int32 i = 0; i < kRefreshRateCount; ++i) {
			if (kRefreshRates[i] < min || kRefreshRates[i] > max)
				continue;

			BString name;
			name << kRefreshRates[i] << " " << B_TRANSLATE("Hz");

			BMessage *message = new BMessage(POP_REFRESH_MSG);
			message->AddFloat("refresh", kRefreshRates[i]);

			fRefreshMenu->AddItem(new BMenuItem(name.String(), message));
		}

		fOtherRefresh = new BMenuItem(B_TRANSLATE("Other" B_UTF8_ELLIPSIS),
			new BMessage(POP_OTHER_REFRESH_MSG));
		fRefreshMenu->AddItem(fOtherRefresh);
	}

	fRefreshField = new BMenuField("RefreshMenu", B_TRANSLATE("Refresh rate:"),
		fRefreshMenu);
	fRefreshField->SetAlignment(B_ALIGN_RIGHT);

	if (_IsVesa())
		fRefreshField->Hide();

	// hardware cursor checkbox: check whether any display mode advertises hardware cursor support
	/*bool supportsHardwareCursor = false;
	for (int32 i = 0; i < fScreenMode.CountModes(); i++) {
		const display_mode &dm = fScreenMode.DisplayModeAt(i);
		if (dm.flags & B_HARDWARE_CURSOR) {
			supportsHardwareCursor = true;
			break;
		}
	}*/

	// create the checkbox (value from saved settings; default true)
	fHardwareCursorCheckbox = new BCheckBox("hardware_cursor",
	B_TRANSLATE("Enable hardware cursor bitmap"), new BMessage(TOGGLE_HARDWARE_CURSOR_MSG));
	// Ask app_server whether the driver exposes B_SET_CURSOR_BITMAP
	bool hasSetCursorBitmap = false;
	bool hasCursorAnimation = false;
	{
		BMessenger msgr("application/x-vnd.haiku-app_server");
		BMessage request('hsbm');
		request.AddInt32("screen", fScreenIndex);
		BMessage reply;
		if (msgr.IsValid() && msgr.SendMessage(&request, &reply, 200000) == B_OK) {
			reply.FindBool("has_set_cursor_bitmap", &hasSetCursorBitmap);
		}

		BMessage requestAni('hcai');
		requestAni.AddInt32("screen", fScreenIndex);
		BMessage replyAni;
		if (msgr.IsValid()
			&& msgr.SendMessage(&requestAni, &replyAni, 200000) == B_OK) {
			replyAni.FindBool("has_cursor_animation", &hasCursorAnimation);
		}
	}
	fHasCursorAnimationCapability = hasCursorAnimation;
	fHardwareCursorCheckbox->SetEnabled(hasSetCursorBitmap);
	if (fSettings->HardwareCursorEnabled(fScreenKey) && hasSetCursorBitmap)
		fHardwareCursorCheckbox->SetValue(B_CONTROL_ON);
	else
		fHardwareCursorCheckbox->SetValue(B_CONTROL_OFF);

	fCursorAnimationCheckbox = new BCheckBox("cursor_animation",
		B_TRANSLATE("Enable hardware cursor animation"),
		new BMessage(TOGGLE_CURSOR_ANIMATION_MSG));
	bool canEnableCursorAnimation = hasSetCursorBitmap
		&& hasCursorAnimation
		&& fHardwareCursorCheckbox->Value() == B_CONTROL_ON;
	fCursorAnimationCheckbox->SetEnabled(canEnableCursorAnimation);
	fCursorAnimationCheckbox->SetValue(canEnableCursorAnimation
		&& fSettings->CursorAnimationEnabled(fScreenKey)
		? B_CONTROL_ON : B_CONTROL_OFF);

	fCursorAnimationSpeedSlider = new BSlider("cursor_ani_speed",
		B_TRANSLATE("Cursor animation speed (%):"),
		new BMessage(SLIDER_CURSOR_ANIMATION_SPEED_MSG), 25, 400, B_HORIZONTAL);
	fCursorAnimationSpeedSlider->SetValue(
		fSettings->CursorAnimationSpeedPercent(fScreenKey));
	fCursorAnimationSpeedSlider->SetEnabled(canEnableCursorAnimation
		&& fCursorAnimationCheckbox->Value() == B_CONTROL_ON);

	fCursorAnimationIdleSlider = new BSlider("cursor_ani_idle",
		B_TRANSLATE("Cursor animation idle delay (ms):"),
		new BMessage(SLIDER_CURSOR_ANIMATION_IDLE_MSG), 0, 15000, B_HORIZONTAL);
	fCursorAnimationIdleSlider->SetValue(
		fSettings->CursorAnimationIdleDelayMs(fScreenKey));
	fCursorAnimationIdleSlider->SetEnabled(canEnableCursorAnimation
		&& fCursorAnimationCheckbox->Value() == B_CONTROL_ON);

	f2DAccelerationCheckbox = new BCheckBox("2d_acceleration",
		B_TRANSLATE("Enable 2D hardware acceleration"),
		new BMessage(TOGGLE_2D_ACCELERATION_MSG));
	bool has2DAcceleration = false;
	{
		BMessenger msgr("application/x-vnd.haiku-app_server");
		BMessage request('h2da');
		request.AddInt32("screen", fScreenIndex);
		BMessage reply;
		if (msgr.IsValid() && msgr.SendMessage(&request, &reply, 200000) == B_OK) {
			reply.FindBool("has_2d_acceleration", &has2DAcceleration);
		}
	}
	f2DAccelerationCheckbox->SetEnabled(has2DAcceleration);
	if (has2DAcceleration && fSettings->Acceleration2DEnabled(fScreenKey))
		f2DAccelerationCheckbox->SetValue(B_CONTROL_ON);
	else
		f2DAccelerationCheckbox->SetValue(B_CONTROL_OFF);

	f2DDebugCheckbox = new BCheckBox("2d_debug",
		B_TRANSLATE("Enable 2D debug indicator"),
		new BMessage(TOGGLE_2D_DEBUG_MSG));

	bool canEnableDebug = has2DAcceleration && (f2DAccelerationCheckbox->Value() == B_CONTROL_ON);
	f2DDebugCheckbox->SetEnabled(canEnableDebug);
	if (canEnableDebug && fSettings->Visual2DDebugEnabled(fScreenKey))
		f2DDebugCheckbox->SetValue(B_CONTROL_ON);
	else
		f2DDebugCheckbox->SetValue(B_CONTROL_OFF);

	f2DCompositorCheckbox = new BCheckBox("2d_compositor",
		B_TRANSLATE("Enable 2D compositor (requires hardware cursor bitmap "
			"on every active GPU)"),
		new BMessage(TOGGLE_2D_COMPOSITOR_MSG));

	// Multi-GPU: the compositor is a single Desktop-wide switch (Fase E),
	// so enabling it requires hardware-cursor support on every currently
	// active GPU, not just this one -- mirrors Desktop::
	// AllActiveGpusHaveHardwareCursor()'s own gate (also enforced
	// server-side now in SetCompositor2DEnabled(), see MULTIGPU_PLAN.md).
	bool canEnableCompositor = _AllActiveGpusHaveHardwareCursor()
		&& fHardwareCursorCheckbox->Value() == B_CONTROL_ON;
	f2DCompositorCheckbox->SetEnabled(canEnableCompositor);
	if (canEnableCompositor && fSettings->Compositor2DEnabled())
		f2DCompositorCheckbox->SetValue(B_CONTROL_ON);
	else
		f2DCompositorCheckbox->SetValue(B_CONTROL_OFF);

	// enlarged area for multi-monitor settings
	{
		bool dummy;
		uint32 dummy32;
		bool multiMonSupport;
		bool useLaptopPanelSupport;
		bool tvStandardSupport;

		multiMonSupport = TestMultiMonSupport(&screen) == B_OK;
		useLaptopPanelSupport = GetUseLaptopPanel(&screen, &dummy) == B_OK;
		tvStandardSupport = GetTVStandard(&screen, &dummy32) == B_OK;

		// even if there is no support, we still create all controls
		// to make sure we don't access NULL pointers later on

		fCombineMenu = new BPopUpMenu("CombineDisplays",
			true, true);

		for (int32 i = 0; i < kCombineModeCount; i++) {
			BMessage *message = new BMessage(POP_COMBINE_DISPLAYS_MSG);
			message->AddInt32("mode", kCombineModes[i].mode);

			fCombineMenu->AddItem(new BMenuItem(kCombineModes[i].name,
				message));
		}

		fCombineField = new BMenuField("CombineMenu",
			B_TRANSLATE("Combine displays:"), fCombineMenu);
		fCombineField->SetAlignment(B_ALIGN_RIGHT);

		if (!multiMonSupport)
			fCombineField->Hide();

		fSwapDisplaysMenu = new BPopUpMenu("SwapDisplays",
			true, true);

		// !order is important - we rely that boolean value == idx
		BMessage *message = new BMessage(POP_SWAP_DISPLAYS_MSG);
		message->AddBool("swap", false);
		fSwapDisplaysMenu->AddItem(new BMenuItem(B_TRANSLATE("no"), message));

		message = new BMessage(POP_SWAP_DISPLAYS_MSG);
		message->AddBool("swap", true);
		fSwapDisplaysMenu->AddItem(new BMenuItem(B_TRANSLATE("yes"), message));

		fSwapDisplaysField = new BMenuField("SwapMenu",
			B_TRANSLATE("Swap displays:"), fSwapDisplaysMenu);
		fSwapDisplaysField->SetAlignment(B_ALIGN_RIGHT);

		if (!multiMonSupport)
			fSwapDisplaysField->Hide();

		fUseLaptopPanelMenu = new BPopUpMenu("UseLaptopPanel",
			true, true);

		// !order is important - we rely that boolean value == idx
		message = new BMessage(POP_USE_LAPTOP_PANEL_MSG);
		message->AddBool("use", false);
		fUseLaptopPanelMenu->AddItem(new BMenuItem(B_TRANSLATE("if needed"),
			message));

		message = new BMessage(POP_USE_LAPTOP_PANEL_MSG);
		message->AddBool("use", true);
		fUseLaptopPanelMenu->AddItem(new BMenuItem(B_TRANSLATE("always"),
			message));

		fUseLaptopPanelField = new BMenuField("UseLaptopPanel",
			B_TRANSLATE("Use laptop panel:"), fUseLaptopPanelMenu);
		fUseLaptopPanelField->SetAlignment(B_ALIGN_RIGHT);

		if (!useLaptopPanelSupport)
			fUseLaptopPanelField->Hide();

		fTVStandardMenu = new BPopUpMenu("TVStandard", true, true);

		// arbitrary limit
		uint32 i;
		for (i = 0; i < 100; ++i) {
			uint32 mode;
			if (GetNthSupportedTVStandard(&screen, i, &mode) != B_OK)
				break;

			BString name = tv_standard_to_string(mode);

			message = new BMessage(POP_TV_STANDARD_MSG);
			message->AddInt32("tv_standard", mode);

			fTVStandardMenu->AddItem(new BMenuItem(name.String(), message));
		}

		fTVStandardField = new BMenuField("tv standard",
			B_TRANSLATE("Video format:"), fTVStandardMenu);
		fTVStandardField->SetAlignment(B_ALIGN_RIGHT);

		if (!tvStandardSupport || i == 0)
			fTVStandardField->Hide();
	}

	BLayoutBuilder::Group<>(outerControlsView)
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.Add(fResolutionField->CreateLabelLayoutItem(), 0, 0)
			.Add(fResolutionField->CreateMenuBarLayoutItem(), 1, 0)
			.Add(fColorsField->CreateLabelLayoutItem(), 0, 1)
			.Add(fColorsField->CreateMenuBarLayoutItem(), 1, 1)
			.Add(fRefreshField->CreateLabelLayoutItem(), 0, 2)
			.Add(fRefreshField->CreateMenuBarLayoutItem(), 1, 2)
			.Add(fCombineField->CreateLabelLayoutItem(), 0, 3)
			.Add(fCombineField->CreateMenuBarLayoutItem(), 1, 3)
			.Add(fSwapDisplaysField->CreateLabelLayoutItem(), 0, 4)
			.Add(fSwapDisplaysField->CreateMenuBarLayoutItem(), 1, 4)
			.Add(fUseLaptopPanelField->CreateLabelLayoutItem(), 0, 5)
			.Add(fUseLaptopPanelField->CreateMenuBarLayoutItem(), 1, 5)
			.Add(fTVStandardField->CreateLabelLayoutItem(), 0, 6)
			.Add(fTVStandardField->CreateMenuBarLayoutItem(), 1, 6)
		.End();

	// add the checkboxes below the grid
	outerControlsView->AddChild(fHardwareCursorCheckbox);
	outerControlsView->AddChild(fCursorAnimationCheckbox);
	outerControlsView->AddChild(fCursorAnimationSpeedSlider);
	outerControlsView->AddChild(fCursorAnimationIdleSlider);
	//outerControlsView->AddChild(f2DAccelerationCheckbox);
	outerControlsView->AddChild(BLayoutBuilder::Group<>(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
		.Add(f2DAccelerationCheckbox)
		.Add(f2DDebugCheckbox)
		.AddGlue()
		.View()
	);
	// f2DCompositorCheckbox intentionally NOT added here: it's a single
	// Desktop-wide switch, not a per-GPU control (see MULTIGPU_PLAN.md
	// Fase E), so it lives in the "Workspaces" tab instead -- built
	// further down, once that tab's page view is assembled.

	// TODO: we don't support getting the screen's preferred settings
	/* fDefaultsButton = new BButton(buttonRect, "DefaultsButton", "Defaults",
		new BMessage(BUTTON_DEFAULTS_MSG));*/

	fApplyButton = new BButton("ApplyButton", B_TRANSLATE("Apply"),
		new BMessage(BUTTON_APPLY_MSG));
	fApplyButton->SetEnabled(false);
	BLayoutBuilder::Group<>(outerControlsView)
		.AddGlue()
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fApplyButton);

	fRevertButton = new BButton("RevertButton", B_TRANSLATE("Revert"),
		new BMessage(BUTTON_REVERT_MSG));
	fRevertButton->SetEnabled(false);

	// Multi-GPU (MULTIGPU_PLAN.md Fase F): "Workspaces" tab (rows/columns +
	// grouped monitor-grid preview + the Desktop-wide compositor
	// checkbox), then one tab per detected GPU. Screen index 0 keeps this
	// window's own original two-column layout (fScreenBox + controlsBox)
	// exactly as before multi-GPU support existed; every other screen
	// gets a GPUSettingsView instead (a simpler, apply-immediately
	// subset -- see that class for why it doesn't try to replicate this
	// window's Apply/Revert/undo staging per GPU).

	fWorkspaceGridView = new WorkspaceGridView("workspace grid");

	// Multi-GPU (MULTIGPU_PLAN.md Fase A, user-configured variant): no
	// automatic primary-GPU detection (that would need a new kernel
	// syscall -- deliberately avoided). The user picks the order/primary
	// here instead; Desktop::_UpdateActiveGpuScreens() applies whatever
	// is persisted via ScreenSettings::SetGpuOrder(). Populated by
	// _RebuildGpuOrderListView(), called once below after the whole
	// window (and app_server's reply to the 'cnts'/'devn'/'gbdf' queries
	// it depends on) is ready.
	BBox* gpuOrderBox = new BBox("gpu order box");
	gpuOrderBox->SetLabel(B_TRANSLATE("GPU order (first = primary)"));

	fGpuOrderListView = new BListView("gpu order list",
		B_SINGLE_SELECTION_LIST);
	BScrollView* gpuOrderScroll = new BScrollView("gpu order scroll",
		fGpuOrderListView, 0, false, true);

	fGpuOrderUpButton = new BButton("gpu order up", B_TRANSLATE("Move up"),
		new BMessage(MOVE_GPU_UP_MSG));
	fGpuOrderDownButton = new BButton("gpu order down",
		B_TRANSLATE("Move down"), new BMessage(MOVE_GPU_DOWN_MSG));

	gpuOrderBox->AddChild(BLayoutBuilder::Group<>(B_HORIZONTAL,
			B_USE_SMALL_SPACING)
		.Add(gpuOrderScroll)
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
			.Add(fGpuOrderUpButton)
			.Add(fGpuOrderDownButton)
			.AddGlue()
			.End()
		.SetInsets(B_USE_DEFAULT_SPACING)
		.View());

	BView* workspacesPage = BLayoutBuilder::Group<>(B_VERTICAL,
			B_USE_DEFAULT_SPACING)
		.Add(workspacesBox)
		.Add(fWorkspaceGridView)
		.Add(gpuOrderBox)
		.Add(f2DCompositorCheckbox)
		.AddGlue()
		.SetInsets(B_USE_WINDOW_SPACING)
		.View();

	BView* screenPage = BLayoutBuilder::Group<>(B_HORIZONTAL,
			B_USE_DEFAULT_SPACING)
		.AddGroup(B_VERTICAL, 0, 1)
			.AddStrut(floorf(controlsBox->TopBorderOffset()
				- fScreenBox->TopBorderOffset()))
			.Add(fScreenBox)
			.AddGlue()
			.End()
		.AddGroup(B_VERTICAL, 0, 1)
			.Add(controlsBox, 2)
			.End()
		.SetInsets(B_USE_WINDOW_SPACING)
		.View();

	BMessenger appServerMessenger("application/x-vnd.haiku-app_server");

	fTabView = new BTabView("tabs", B_WIDTH_FROM_WIDEST);

	// BTab::SetLabel() is a no-op until the tab has a view (fView == NULL
	// guard in TabView.cpp), which only happens once AddTab() calls
	// SetView() -- so SetLabel() must come AFTER AddTab(), not before.
	BTab* workspacesTab = new BTab();
	fTabView->AddTab(workspacesPage, workspacesTab);
	workspacesTab->SetLabel(B_TRANSLATE("Workspaces"));

	BTab* screenTab = new BTab();
	fTabView->AddTab(screenPage, screenTab);
	screenTab->SetLabel(gpu_device_name(appServerMessenger, fScreenIndex));

	int32 gpuCount = 1;
	{
		BMessage request('cnts');
		BMessage reply;
		if (appServerMessenger.IsValid()
			&& appServerMessenger.SendMessage(&request, &reply, 200000)
				== B_OK) {
			reply.FindInt32("count", &gpuCount);
		}
	}

	// Every OTHER detected screen gets the simplified GPUSettingsView tab
	// -- "other" meaning "not fScreenIndex" (the effective primary, see
	// primary_screen_index() above), not "not index 0".
	for (int32 i = 0; i < gpuCount; i++) {
		if (i == fScreenIndex)
			continue;
		BString name = gpu_device_name(appServerMessenger, i);
		GPUSettingsView* gpuView = new GPUSettingsView(name, this, i,
			fSettings);
		BTab* tab = new BTab();
		fTabView->AddTab(gpuView, tab);
		tab->SetLabel(name);
	}

	_UpdateWorkspaceGridView();
	_RebuildGpuOrderListView();

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.Add(fTabView)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(fRevertButton)
			.AddGlue()
			.End()
		.SetInsets(B_USE_WINDOW_SPACING);

	_UpdateControls();
	_UpdateMonitor();
	_RefreshEdidGatedControls();

	MoveOnScreen();
}


ScreenWindow::~ScreenWindow()
{
	delete fSettings;
	delete fDumpFilePanel;
	delete fLoadFilePanel;
}


bool
ScreenWindow::QuitRequested()
{
	fSettings->SetWindowFrame(Frame());

	// Write mode of workspace 0 (the boot workspace) to the vesa settings file
	screen_mode vesaMode;
	if (fBootWorkspaceApplied && fScreenMode.Get(vesaMode, 0) == B_OK) {
		status_t status = _WriteVesaModeFile(vesaMode);
		if (status < B_OK) {
			BString warning = B_TRANSLATE("Could not write VESA mode settings"
				" file:\n\t");
			warning << strerror(status);
			BAlert* alert = new BAlert(B_TRANSLATE("Warning"),
				warning.String(), B_TRANSLATE("OK"), NULL,
				NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();
		}
	}

	be_app->PostMessage(B_QUIT_REQUESTED);

	return BWindow::QuitRequested();
}


/*!	Update resolution list according to combine mode
	(some resolutions may not be combinable due to memory restrictions).
*/
void
ScreenWindow::_CheckResolutionMenu()
{
	for (int32 i = 0; i < fResolutionMenu->CountItems(); i++)
		fResolutionMenu->ItemAt(i)->SetEnabled(false);

	for (int32 i = 0; i < fScreenMode.CountModes(); i++) {
		screen_mode mode = fScreenMode.ModeAt(i);
		if (mode.combine != fSelected.combine)
			continue;

		BString name;
		name.SetToFormat(B_TRANSLATE_COMMENT("%" B_PRId32" × %" B_PRId32,
			"The '×' is the Unicode multiplication sign U+00D7"),
			mode.width, mode.height);

		BMenuItem *item = fResolutionMenu->FindItem(name.String());
		if (item != NULL)
			item->SetEnabled(true);
	}
}


/*!	Update color and refresh options according to current mode
	(a color space is made active if there is any mode with
	given resolution and this colour space; same applies for
	refresh rate, though "Other…" is always possible)
*/
void
ScreenWindow::_CheckColorMenu()
{
	int32 supportsAnything = false;
	int32 index = 0;

	for (int32 i = 0; i < kColorSpaceCount; i++) {
		if ((fSupportedColorSpaces & (1 << i)) == 0)
			continue;

		bool supported = false;

		for (int32 j = 0; j < fScreenMode.CountModes(); j++) {
			screen_mode mode = fScreenMode.ModeAt(j);

			if (fSelected.width == mode.width
				&& fSelected.height == mode.height
				&& kColorSpaces[i].space == mode.space
				&& fSelected.combine == mode.combine) {
				supportsAnything = true;
				supported = true;
				break;
			}
		}

		BMenuItem* item = fColorsMenu->ItemAt(index++);
		if (item)
			item->SetEnabled(supported);
	}

	fColorsField->SetEnabled(supportsAnything);

	if (!supportsAnything)
		return;

	// Make sure a valid item is selected

	BMenuItem* item = fColorsMenu->FindMarked();
	bool changed = false;

	if (item != fUserSelectedColorSpace) {
		if (fUserSelectedColorSpace != NULL
			&& fUserSelectedColorSpace->IsEnabled()) {
			fUserSelectedColorSpace->SetMarked(true);
			item = fUserSelectedColorSpace;
			changed = true;
		}
	}
	if (item != NULL && !item->IsEnabled()) {
		// find the next best item
		int32 index = fColorsMenu->IndexOf(item);
		bool found = false;

		for (int32 i = index + 1; i < fColorsMenu->CountItems(); i++) {
			item = fColorsMenu->ItemAt(i);
			if (item->IsEnabled()) {
				found = true;
				break;
			}
		}
		if (!found) {
			// search backwards as well
			for (int32 i = index - 1; i >= 0; i--) {
				item = fColorsMenu->ItemAt(i);
				if (item->IsEnabled())
					break;
			}
		}

		item->SetMarked(true);
		changed = true;
	}

	if (changed) {
		// Update selected space

		BMessage* message = item->Message();
		int32 space;
		if (message->FindInt32("space", &space) == B_OK) {
			fSelected.space = (color_space)space;
			_UpdateColorLabel();
		}
	}
}


/*!	Enable/disable refresh options according to current mode. */
void
ScreenWindow::_CheckRefreshMenu()
{
	float min, max;
	if (fScreenMode.GetRefreshLimits(fSelected, min, max) != B_OK || min == max)
		return;

	for (int32 i = fRefreshMenu->CountItems(); i-- > 0;) {
		BMenuItem* item = fRefreshMenu->ItemAt(i);
		BMessage* message = item->Message();
		float refresh;
		if (message != NULL && message->FindFloat("refresh", &refresh) == B_OK)
			item->SetEnabled(refresh >= min && refresh <= max);
	}
}


/*!	Activate appropriate menu item according to selected refresh rate */
void
ScreenWindow::_UpdateRefreshControl()
{
	if (isnan(fSelected.refresh)) {
		fRefreshMenu->SetEnabled(false);
		fOtherRefresh->SetLabel(B_TRANSLATE("Unknown"));
		fOtherRefresh->SetMarked(true);
		return;
	} else {
		fRefreshMenu->SetEnabled(true);
	}

	for (int32 i = 0; i < fRefreshMenu->CountItems(); i++) {
		BMenuItem* item = fRefreshMenu->ItemAt(i);
		if (item->Message()->FindFloat("refresh") == fSelected.refresh) {
			item->SetMarked(true);
			// "Other" items only contains a refresh rate when active
			if (fOtherRefresh != NULL)
				fOtherRefresh->SetLabel(B_TRANSLATE("Other" B_UTF8_ELLIPSIS));
			return;
		}
	}
	
	// this is a non-standard refresh rate
	if (fOtherRefresh != NULL) {
		fOtherRefresh->Message()->ReplaceFloat("refresh", fSelected.refresh);
		fOtherRefresh->SetMarked(true);

		BString string;
		refresh_rate_to_string(fSelected.refresh, string);
		fRefreshMenu->Superitem()->SetLabel(string.String());

		string.Append(B_TRANSLATE("/other" B_UTF8_ELLIPSIS));
		fOtherRefresh->SetLabel(string.String());
	}
}


void
ScreenWindow::_UpdateMonitorView()
{
	BMessage updateMessage(UPDATE_DESKTOP_MSG);
	updateMessage.AddInt32("width", fSelected.width);
	updateMessage.AddInt32("height", fSelected.height);

	PostMessage(&updateMessage, fMonitorView);
}


void
ScreenWindow::_UpdateControls()
{
	_UpdateWorkspaceButtons();

	BMenuItem* item = fSwapDisplaysMenu->ItemAt((int32)fSelected.swap_displays);
	if (item != NULL && !item->IsMarked())
		item->SetMarked(true);

	item = fUseLaptopPanelMenu->ItemAt((int32)fSelected.use_laptop_panel);
	if (item != NULL && !item->IsMarked())
		item->SetMarked(true);

	for (int32 i = 0; i < fTVStandardMenu->CountItems(); i++) {
		item = fTVStandardMenu->ItemAt(i);

		uint32 tvStandard;
		item->Message()->FindInt32("tv_standard", (int32 *)&tvStandard);
		if (tvStandard == fSelected.tv_standard) {
			if (!item->IsMarked())
				item->SetMarked(true);
			break;
		}
	}

	_CheckResolutionMenu();
	_CheckColorMenu();
	_CheckRefreshMenu();

	BString string;
	resolution_to_string(fSelected, string);
	item = fResolutionMenu->FindItem(string.String());

	if (item != NULL) {
		if (!item->IsMarked())
			item->SetMarked(true);
	} else {
		// this is bad luck - if mode has been set via screen references,
		// this case cannot occur; there are three possible solutions:
		// 1. add a new resolution to list
		//    - we had to remove it as soon as a "valid" one is selected
		//    - we don't know which frequencies/bit depths are supported
		//    - as long as we haven't the GMT formula to create
		//      parameters for any resolution given, we cannot
		//      really set current mode - it's just not in the list
		// 2. choose nearest resolution
		//    - probably a good idea, but implies coding and testing
		// 3. choose lowest resolution
		//    - do you really think we are so lazy? yes, we are
		item = fResolutionMenu->ItemAt(0);
		if (item)
			item->SetMarked(true);

		// okay - at least we set menu label to active resolution
		fResolutionMenu->Superitem()->SetLabel(string.String());
	}

	// mark active combine mode
	for (int32 i = 0; i < kCombineModeCount; i++) {
		if (kCombineModes[i].mode == fSelected.combine) {
			item = fCombineMenu->ItemAt(i);
			if (item != NULL && !item->IsMarked())
				item->SetMarked(true);
			break;
		}
	}

	item = fColorsMenu->ItemAt(0);

	for (int32 i = 0, index = 0; i <  kColorSpaceCount; i++) {
		if ((fSupportedColorSpaces & (1 << i)) == 0)
			continue;

		if (kColorSpaces[i].space == fSelected.space) {
			item = fColorsMenu->ItemAt(index);
			break;
		}

		index++;
	}

	if (item != NULL && !item->IsMarked())
		item->SetMarked(true);

	_UpdateColorLabel();
	_UpdateMonitorView();
	_UpdateRefreshControl();

	_CheckApplyEnabled();
}


/*! Reflect active mode in chosen settings */
void
ScreenWindow::_UpdateActiveMode()
{
	_UpdateActiveMode(current_workspace());
}


void
ScreenWindow::_UpdateActiveMode(int32 workspace)
{
	// Usually, this function gets called after a mode
	// has been set manually; still, as the graphics driver
	// is free to fiddle with mode passed, we better ask
	// what kind of mode we actually got
	if (fScreenMode.Get(fActive, workspace) == B_OK) {
		fSelected = fActive;

		_UpdateMonitor();
		_BuildSupportedColorSpaces();
		_UpdateControls();
	}
}


void
ScreenWindow::_UpdateWorkspaceButtons()
{
	uint32 columns;
	uint32 rows;
	BPrivate::get_workspaces_layout(&columns, &rows);

	// Set the max values enabling/disabling the up/down arrows

	if (rows == 1)
		fColumnsControl->SetMaxValue(32);
	else if (rows == 2)
		fColumnsControl->SetMaxValue(16);
	else if (rows <= 4)
		fColumnsControl->SetMaxValue(8);
	else if (rows <= 8)
		fColumnsControl->SetMaxValue(4);
	else if (rows <= 16)
		fColumnsControl->SetMaxValue(2);
	else if (rows <= 32)
		fColumnsControl->SetMaxValue(1);

	if (columns == 1)
		fRowsControl->SetMaxValue(32);
	else if (columns == 2)
		fRowsControl->SetMaxValue(16);
	else if (columns <= 4)
		fRowsControl->SetMaxValue(8);
	else if (columns <= 8)
		fRowsControl->SetMaxValue(4);
	else if (columns <= 16)
		fRowsControl->SetMaxValue(2);
	else if (columns <= 32)
		fRowsControl->SetMaxValue(1);
}


/*!	Queries app_server for every detected screen's name and PCI BDF, then
	applies the user-configured order from ScreenSettings::GpuOrder() (see
	MULTIGPU_PLAN.md Fase A) the exact same way Desktop::
	_UpdateActiveGpuScreens() does server-side: configured entries first
	(primary == index 0), then every screen not in the configured list
	appended in discovery order. Keeping both places' logic in lock-step
	is what makes the "Workspaces" tab's preview and GPU-order list
	actually match what app_server will do.
*/
void
ScreenWindow::_GetEffectiveGpuOrder(BStringList& names, BStringList& bdfs) const
{
	names.MakeEmpty();
	bdfs.MakeEmpty();

	BMessenger msgr("application/x-vnd.haiku-app_server");

	int32 gpuCount = 0;
	{
		BMessage request('cnts');
		BMessage reply;
		if (msgr.IsValid()
			&& msgr.SendMessage(&request, &reply, 200000) == B_OK) {
			reply.FindInt32("count", &gpuCount);
		}
	}

	BStringList discoveredNames, discoveredBdfs;
	for (int32 i = 0; i < gpuCount; i++) {
		BString name;
		BMessage nameRequest('devn');
		nameRequest.AddInt32("screen", i);
		BMessage nameReply;
		if (msgr.SendMessage(&nameRequest, &nameReply, 200000) == B_OK)
			nameReply.FindString("name", &name);
		if (name.IsEmpty())
			name.SetToFormat(B_TRANSLATE("GPU %" B_PRId32), i);
		discoveredNames.Add(name);

		BString bdf;
		BMessage bdfRequest('gbdf');
		bdfRequest.AddInt32("screen", i);
		BMessage bdfReply;
		if (msgr.SendMessage(&bdfRequest, &bdfReply, 200000) == B_OK)
			bdfReply.FindString("bdf", &bdf);
		discoveredBdfs.Add(bdf);
	}

	BStringList configuredOrder = fSettings->GpuOrder();

	for (int32 i = 0; i < configuredOrder.CountStrings(); i++) {
		const BString& wanted = configuredOrder.StringAt(i);
		if (wanted.IsEmpty() || bdfs.HasString(wanted))
			continue;

		int32 j = discoveredBdfs.IndexOf(wanted);
		if (j < 0)
			continue;

		names.Add(discoveredNames.StringAt(j));
		bdfs.Add(discoveredBdfs.StringAt(j));
	}
	for (int32 j = 0; j < discoveredBdfs.CountStrings(); j++) {
		const BString& bdf = discoveredBdfs.StringAt(j);
		if (bdf.IsEmpty() || !bdfs.HasString(bdf)) {
			names.Add(discoveredNames.StringAt(j));
			bdfs.Add(bdf);
		}
	}
}


void
ScreenWindow::_UpdateWorkspaceGridView()
{
	if (fWorkspaceGridView == NULL)
		return;

	uint32 columns, rows;
	BPrivate::get_workspaces_layout(&columns, &rows);

	BStringList names, bdfs;
	_GetEffectiveGpuOrder(names, bdfs);

	fWorkspaceGridView->SetGridLayout(columns, rows, names);
}


void
ScreenWindow::_RebuildGpuOrderListView()
{
	if (fGpuOrderListView == NULL)
		return;

	BStringList names, bdfs;
	_GetEffectiveGpuOrder(names, bdfs);

	fGpuOrderListView->MakeEmpty();
	fGpuOrderBdfs.MakeEmpty();

	for (int32 i = 0; i < names.CountStrings(); i++) {
		fGpuOrderListView->AddItem(new BStringItem(names.StringAt(i)));
		fGpuOrderBdfs.Add(bdfs.StringAt(i));
	}
}


void
ScreenWindow::_MoveSelectedGpu(int32 delta)
{
	if (fGpuOrderListView == NULL)
		return;

	int32 selected = fGpuOrderListView->CurrentSelection();
	int32 target = selected + delta;
	if (selected < 0 || target < 0
		|| target >= fGpuOrderListView->CountItems()) {
		return;
	}

	BListItem* item = fGpuOrderListView->RemoveItem(selected);
	fGpuOrderListView->AddItem(item, target);
	fGpuOrderListView->Select(target);

	BString bdf = fGpuOrderBdfs.StringAt(selected);
	fGpuOrderBdfs.Remove(selected, 1);
	fGpuOrderBdfs.Add(bdf, target);

	fSettings->SetGpuOrder(fGpuOrderBdfs);
	_UpdateWorkspaceGridView();
}


void
ScreenWindow::ScreenChanged(BRect frame, color_space mode)
{
	// move window on screen, if necessary
	if (frame.right <= Frame().right
		&& frame.bottom <= Frame().bottom) {
		MoveTo((frame.Width() - Frame().Width()) / 2,
			(frame.Height() - Frame().Height()) / 2);
	}
}


void
ScreenWindow::WorkspaceActivated(int32 workspace, bool state)
{
	if (fScreenMode.GetOriginalMode(fOriginal, workspace) == B_OK) {
		_UpdateActiveMode(workspace);

		BMessage message(UPDATE_DESKTOP_COLOR_MSG);
		PostMessage(&message, fMonitorView);
	}
}


void
ScreenWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case WORKSPACE_CHECK_MSG:
			_CheckApplyEnabled();
			break;

		case kMsgWorkspaceColumnsChanged:
		{
			uint32 newColumns = (uint32)fColumnsControl->Value();

			uint32 rows;
			BPrivate::get_workspaces_layout(NULL, &rows);
			BPrivate::set_workspaces_layout(newColumns, rows);

			_UpdateWorkspaceButtons();
			fRowsControl->SetValue(rows);
				// enables/disables up/down arrows
			_CheckApplyEnabled();
			_UpdateWorkspaceGridView();

			break;
		}

		case kMsgWorkspaceRowsChanged:
		{
			uint32 newRows = (uint32)fRowsControl->Value();

			uint32 columns;
			BPrivate::get_workspaces_layout(&columns, NULL);
			BPrivate::set_workspaces_layout(columns, newRows);

			_UpdateWorkspaceButtons();
			fColumnsControl->SetValue(columns);
				// enables/disables up/down arrows
			_CheckApplyEnabled();
			_UpdateWorkspaceGridView();
			break;
		}

		case MOVE_GPU_UP_MSG:
			_MoveSelectedGpu(-1);
			break;

		case MOVE_GPU_DOWN_MSG:
			_MoveSelectedGpu(1);
			break;

		case POP_RESOLUTION_MSG:
		{
			message->FindInt32("width", &fSelected.width);
			message->FindInt32("height", &fSelected.height);

			_CheckColorMenu();
			_CheckRefreshMenu();

			_UpdateMonitorView();
			_UpdateRefreshControl();

			_CheckApplyEnabled();
			break;
		}

		case POP_COLORS_MSG:
		{
			int32 space;
			if (message->FindInt32("space", &space) != B_OK)
				break;

			int32 index;
			if (message->FindInt32("index", &index) == B_OK
				&& fColorsMenu->ItemAt(index) != NULL)
				fUserSelectedColorSpace = fColorsMenu->ItemAt(index);

			fSelected.space = (color_space)space;
			_UpdateColorLabel();

			_CheckApplyEnabled();
			break;
		}

		case POP_REFRESH_MSG:
		{
			message->FindFloat("refresh", &fSelected.refresh);
			fOtherRefresh->SetLabel(B_TRANSLATE("Other" B_UTF8_ELLIPSIS));
				// revert "Other…" label - it might have a refresh rate prefix

			_CheckApplyEnabled();
			break;
		}

		case POP_OTHER_REFRESH_MSG:
		{
			// make sure menu shows something useful
			_UpdateRefreshControl();

			float min = 0, max = 999;
			fScreenMode.GetRefreshLimits(fSelected, min, max);
			if (min < gMinRefresh)
				min = gMinRefresh;
			if (max > gMaxRefresh)
				max = gMaxRefresh;

			monitor_info info;
			if (fScreenMode.GetMonitorInfo(info) == B_OK) {
				min = max_c(info.min_vertical_frequency, min);
				max = min_c(info.max_vertical_frequency, max);
			}

			RefreshWindow *fRefreshWindow = new RefreshWindow(
				fRefreshField->ConvertToScreen(B_ORIGIN), fSelected.refresh,
				min, max);
			fRefreshWindow->Show();
			break;
		}

		case SET_CUSTOM_REFRESH_MSG:
		{
			// user pressed "done" in "Other…" refresh dialog;
			// select the refresh rate chosen
			message->FindFloat("refresh", &fSelected.refresh);

			_UpdateRefreshControl();
			_CheckApplyEnabled();
			break;
		}

		case POP_COMBINE_DISPLAYS_MSG:
		{
			// new combine mode has bee chosen
			int32 mode;
			if (message->FindInt32("mode", &mode) == B_OK)
				fSelected.combine = (combine_mode)mode;

			_CheckResolutionMenu();
			_CheckApplyEnabled();
			break;
		}

		case POP_SWAP_DISPLAYS_MSG:
			message->FindBool("swap", &fSelected.swap_displays);
			_CheckApplyEnabled();
			break;

		case POP_USE_LAPTOP_PANEL_MSG:
			message->FindBool("use", &fSelected.use_laptop_panel);
			_CheckApplyEnabled();
			break;

		case POP_TV_STANDARD_MSG:
			message->FindInt32("tv_standard", (int32 *)&fSelected.tv_standard);
			_CheckApplyEnabled();
			break;

		case BUTTON_LAUNCH_BACKGROUNDS_MSG:
			if (be_roster->Launch(kBackgroundsSignature) == B_ALREADY_RUNNING) {
				app_info info;
				be_roster->GetAppInfo(kBackgroundsSignature, &info);
				be_roster->ActivateApp(info.team);
			}
			break;

		case BUTTON_DEFAULTS_MSG:
		{
			// TODO: get preferred settings of screen
			fSelected.width = 640;
			fSelected.height = 480;
			fSelected.space = B_CMAP8;
			fSelected.refresh = 60.0;
			fSelected.combine = kCombineDisable;
			fSelected.swap_displays = false;
			fSelected.use_laptop_panel = false;
			fSelected.tv_standard = 0;

			// TODO: workspace defaults

			_UpdateControls();
			break;
		}

		case BUTTON_UNDO_MSG:
			fUndoScreenMode.Revert();
			_UpdateActiveMode();
			break;

		case BUTTON_REVERT_MSG:
		{
			fModified = false;
			fBootWorkspaceApplied = false;

			// ScreenMode::Revert() assumes that we first set the correct
			// number of workspaces

			BPrivate::set_workspaces_layout(fOriginalWorkspacesColumns,
				fOriginalWorkspacesRows);
			_UpdateWorkspaceButtons();
			_UpdateWorkspaceGridView();

			fScreenMode.Revert();

			BScreen screen(this);
			screen.SetBrightness(fOriginalBrightness);
			fBrightnessSlider->SetValue(fOriginalBrightness * 255);

			_UpdateActiveMode();
			break;
		}

		case BUTTON_APPLY_MSG:
			_Apply();
			break;

		case MAKE_INITIAL_MSG:
			// user pressed "keep" in confirmation dialog
			fModified = true;
			_UpdateActiveMode();
			break;

		case UPDATE_DESKTOP_COLOR_MSG:
			PostMessage(message, fMonitorView);
			break;

		case SLIDER_BRIGHTNESS_MSG:
		{
			BScreen screen(this);
			screen.SetBrightness(message->FindInt32("be:value") / 255.f);
			_CheckApplyEnabled();
			break;
		}

		case TOGGLE_HARDWARE_CURSOR_MSG:
		{
			bool enabled = fHardwareCursorCheckbox->Value() == B_CONTROL_ON;
			fSettings->SetHardwareCursorEnabled(fScreenKey, fScreenIndex,
				enabled);

			bool canEnableCursorAnimation = enabled
				&& fHasCursorAnimationCapability;
			fCursorAnimationCheckbox->SetEnabled(canEnableCursorAnimation);
			if (!canEnableCursorAnimation) {
				fCursorAnimationCheckbox->SetValue(B_CONTROL_OFF);
				fCursorAnimationSpeedSlider->SetEnabled(false);
				fCursorAnimationIdleSlider->SetEnabled(false);
				fSettings->SetCursorAnimationEnabled(fScreenKey, fScreenIndex,
					false);
			} else {
				fCursorAnimationCheckbox->SetValue(
					fSettings->CursorAnimationEnabled(fScreenKey)
						? B_CONTROL_ON : B_CONTROL_OFF);
				fCursorAnimationSpeedSlider->SetEnabled(
					fCursorAnimationCheckbox->Value() == B_CONTROL_ON);
				fCursorAnimationIdleSlider->SetEnabled(
					fCursorAnimationCheckbox->Value() == B_CONTROL_ON);
			}
			
			if (enabled && _AllActiveGpusHaveHardwareCursor()) {
				f2DCompositorCheckbox->SetEnabled(true);
			} else {
				f2DCompositorCheckbox->SetValue(B_CONTROL_OFF);
				f2DCompositorCheckbox->SetEnabled(false);
				fSettings->SetCompositor2DEnabled(false);
			}
			break;
		}

		case TOGGLE_CURSOR_ANIMATION_MSG:
		{
			bool enabled = fCursorAnimationCheckbox->Value() == B_CONTROL_ON;
			fSettings->SetCursorAnimationEnabled(fScreenKey, fScreenIndex,
				enabled);
			fCursorAnimationSpeedSlider->SetEnabled(enabled);
			fCursorAnimationIdleSlider->SetEnabled(enabled);
			break;
		}

		case SLIDER_CURSOR_ANIMATION_SPEED_MSG:
		{
			fSettings->SetCursorAnimationSpeedPercent(fScreenKey, fScreenIndex,
				fCursorAnimationSpeedSlider->Value());
			break;
		}

		case SLIDER_CURSOR_ANIMATION_IDLE_MSG:
		{
			fSettings->SetCursorAnimationIdleDelayMs(fScreenKey, fScreenIndex,
				fCursorAnimationIdleSlider->Value());
			break;
		}

		case TOGGLE_2D_ACCELERATION_MSG:
		{
			bool enabled = f2DAccelerationCheckbox->Value() == B_CONTROL_ON;
			fSettings->SetAcceleration2DEnabled(fScreenKey, fScreenIndex,
				enabled);

			if (enabled) {
				f2DDebugCheckbox->SetEnabled(true);
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

		case TOGGLE_2D_COMPOSITOR_MSG:
		{
			bool enabled = f2DCompositorCheckbox->Value() == B_CONTROL_ON;
			fSettings->SetCompositor2DEnabled(enabled);
			break;
		}

		case BUTTON_DUMP_EDID_MSG:
			if (fDumpFilePanel == NULL) {
				BMessenger messenger(this);
				fDumpFilePanel = new BFilePanel(B_SAVE_PANEL, &messenger, NULL,
					B_FILE_NODE, false, NULL);
				fDumpFilePanel->SetSaveText("edid_info.bin");
			}
			fDumpFilePanel->Show();
			break;

		case BUTTON_LOAD_EDID_MSG:
			if (fLoadFilePanel == NULL) {
				BMessenger messenger(this);
				fLoadFilePanel = new BFilePanel(B_OPEN_PANEL, &messenger, NULL,
					B_FILE_NODE, false, NULL);
			}
			fLoadFilePanel->Show();
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
			if (message->FindRef("refs", &ref) == B_OK) {
				_LoadEDID(&ref);
			}
			break;
		}

		default:
			BWindow::MessageReceived(message);
	}
}


void
ScreenWindow::_DumpEDID(const entry_ref* dirRef, const char* name)
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
ScreenWindow::_LoadEDID(const entry_ref* ref)
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
		BString errStr(B_TRANSLATE("Driver failed to load EDID:\n\t"));
		errStr << strerror(status);
		BAlert* alert = new BAlert(B_TRANSLATE("Error"), errStr.String(),
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
		return;
	}

	fScreenMode.UpdateModeList();
	_BuildSupportedColorSpaces();
	_CheckResolutionMenu();
	_CheckRefreshMenu();
	_CheckColorMenu();
	_UpdateControls();
	_UpdateMonitor();
	_UpdateActiveMode();
	_RefreshEdidGatedControls();

	_OfferToSaveStableEdid(edid);
		// its own BAlert covers "EDID successfully loaded", no need for
		// a second one here
}


/*!	See GPUSettingsView::_QueryHasEDID() -- same 'hedi' query, factored
	per-window since ScreenWindow's screen-0 tab doesn't share a class
	with GPUSettingsView's secondary-GPU tabs.
*/
bool
ScreenWindow::_QueryHasEDID(int32 screenIndex) const
{
	bool hasEDID = false;
	BMessenger msgr("application/x-vnd.haiku-app_server");
	BMessage request('hedi');
	request.AddInt32("screen", screenIndex);
	BMessage reply;
	if (msgr.IsValid() && msgr.SendMessage(&request, &reply, 200000) == B_OK)
		reply.FindBool("has_edid", &hasEDID);
	return hasEDID;
}


/*!	Mirrors Desktop::AllActiveGpusHaveHardwareCursor() client-side, for
	gating the "Enable 2D compositor" checkbox -- see MULTIGPU_PLAN.md
	Fase E. "Active" here means effective-EDID (_QueryHasEDID()), same
	definition Desktop uses for activeGpuList.
*/
bool
ScreenWindow::_AllActiveGpusHaveHardwareCursor() const
{
	BMessenger msgr("application/x-vnd.haiku-app_server");

	int32 gpuCount = 0;
	BMessage request('cnts');
	BMessage reply;
	if (msgr.IsValid() && msgr.SendMessage(&request, &reply, 200000) == B_OK)
		reply.FindInt32("count", &gpuCount);

	for (int32 i = 0; i < gpuCount; i++) {
		if (!_QueryHasEDID(i))
			continue;

		bool hasSetCursorBitmap = false;
		BMessage request2('hsbm');
		request2.AddInt32("screen", i);
		BMessage reply2;
		if (msgr.SendMessage(&request2, &reply2, 200000) == B_OK)
			reply2.FindBool("has_set_cursor_bitmap", &hasSetCursorBitmap);

		if (!hasSetCursorBitmap)
			return false;
	}

	return true;
}


/*!	Without an EDID, the driver has no idea what modes/cursor/2D behavior
	are actually safe, so every control that depends on that is disabled
	until one is assigned -- see MULTIGPU_PLAN.md EDID section. Mirrors
	GPUSettingsView::_RefreshEdidGatedControls() for this window's own
	screen-0 controls (resolution/colors/refresh/combine/swap/laptop-
	panel/TV-standard/brightness/cursor/2D); Dump/Load EDID stay enabled
	regardless (that's how you assign one in the first place).
*/
void
ScreenWindow::_RefreshEdidGatedControls()
{
	bool hasEDID = _QueryHasEDID(fScreenIndex);

	fResolutionField->SetEnabled(hasEDID);
	fColorsField->SetEnabled(hasEDID);
	fRefreshField->SetEnabled(hasEDID);
	fCombineField->SetEnabled(hasEDID);
	fSwapDisplaysField->SetEnabled(hasEDID);
	fUseLaptopPanelField->SetEnabled(hasEDID);
	fTVStandardField->SetEnabled(hasEDID);
	fBrightnessSlider->SetEnabled(hasEDID);

	bool hasSetCursorBitmap = false;
	{
		BMessenger msgr("application/x-vnd.haiku-app_server");
		BMessage request('hsbm');
		request.AddInt32("screen", fScreenIndex);
		BMessage reply;
		if (msgr.IsValid() && msgr.SendMessage(&request, &reply, 200000) == B_OK)
			reply.FindBool("has_set_cursor_bitmap", &hasSetCursorBitmap);
	}
	fHardwareCursorCheckbox->SetEnabled(hasEDID && hasSetCursorBitmap);
	if (!hasEDID) {
		fCursorAnimationCheckbox->SetEnabled(false);
		fCursorAnimationSpeedSlider->SetEnabled(false);
		fCursorAnimationIdleSlider->SetEnabled(false);
	}

	bool has2DAcceleration = false;
	{
		BMessenger msgr("application/x-vnd.haiku-app_server");
		BMessage request('h2da');
		request.AddInt32("screen", fScreenIndex);
		BMessage reply;
		if (msgr.IsValid() && msgr.SendMessage(&request, &reply, 200000) == B_OK)
			reply.FindBool("has_2d_acceleration", &has2DAcceleration);
	}
	f2DAccelerationCheckbox->SetEnabled(hasEDID && has2DAcceleration);
	if (!hasEDID) {
		f2DDebugCheckbox->SetEnabled(false);
	} else {
		f2DDebugCheckbox->SetEnabled(has2DAcceleration
			&& f2DAccelerationCheckbox->Value() == B_CONTROL_ON);
	}

	bool canEnableCompositor = hasEDID && _AllActiveGpusHaveHardwareCursor()
		&& fHardwareCursorCheckbox->Value() == B_CONTROL_ON;
	f2DCompositorCheckbox->SetEnabled(canEnableCompositor);
}


/*!	See GPUSettingsView::_OfferToSaveStableEdid() -- identical design,
	just this window's own screen (fScreenIndex, almost always 0).
*/
void
ScreenWindow::_OfferToSaveStableEdid(const edid1_info& edid)
{
	BAlert* offer = new BAlert(B_TRANSLATE("Load EDID"),
		B_TRANSLATE("EDID successfully loaded for this session. "
			"Make it permanent (load automatically on every boot)?"),
		B_TRANSLATE("Just this session"), B_TRANSLATE("Make permanent"),
		NULL, B_WIDTH_AS_USUAL, B_IDEA_ALERT);
	if (offer->Go() != 1)
		return;

	BString bdf;
	{
		BMessenger msgr("application/x-vnd.haiku-app_server");
		BMessage request('gbdf');
		request.AddInt32("screen", fScreenIndex);
		BMessage reply;
		if (msgr.IsValid() && msgr.SendMessage(&request, &reply, 200000) == B_OK)
			reply.FindString("bdf", &bdf);
	}

	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;
	path.Append("edid");
	create_directory(path.Path(), 0755);

	BString fileName(bdf.IsEmpty() ? "default" : bdf.String());
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


status_t
ScreenWindow::_WriteVesaModeFile(const screen_mode& mode) const
{
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path, true);
	if (status < B_OK)
		return status;

	path.Append("kernel/drivers");
	status = create_directory(path.Path(), 0755);
	if (status < B_OK)
		return status;

	path.Append("vesa");
	BFile file;
	status = file.SetTo(path.Path(), B_CREATE_FILE | B_WRITE_ONLY | B_ERASE_FILE);
	if (status < B_OK)
		return status;

	char buffer[256];
	snprintf(buffer, sizeof(buffer), "mode %" B_PRId32 " %" B_PRId32 " %"
		B_PRId32 "\n", mode.width, mode.height, mode.BitsPerPixel());

	ssize_t bytesWritten = file.Write(buffer, strlen(buffer));
	if (bytesWritten < B_OK)
		return bytesWritten;

	return B_OK;
}


void
ScreenWindow::_BuildSupportedColorSpaces()
{
	fSupportedColorSpaces = 0;

	for (int32 i = 0; i < kColorSpaceCount; i++) {
		for (int32 j = 0; j < fScreenMode.CountModes(); j++) {
			if (fScreenMode.ModeAt(j).space == kColorSpaces[i].space) {
				fSupportedColorSpaces |= 1 << i;
				break;
			}
		}
	}
}


void
ScreenWindow::_CheckApplyEnabled()
{
	bool applyEnabled = true;

	if (fSelected == fActive) {
		applyEnabled = false;
		if (fAllWorkspacesItem->IsMarked()) {
			screen_mode screenMode;
			const int32 workspaceCount = count_workspaces();
			for (int32 i = 0; i < workspaceCount; i++) {
				fScreenMode.Get(screenMode, i);
				if (screenMode != fSelected) {
					applyEnabled = true;
					break;
				}
			}
		}
	}

	fApplyButton->SetEnabled(applyEnabled);

	uint32 columns;
	uint32 rows;
	BPrivate::get_workspaces_layout(&columns, &rows);

	BScreen screen(this);
	float brightness = -1;
	screen.GetBrightness(&brightness);

	fRevertButton->SetEnabled(columns != fOriginalWorkspacesColumns
		|| rows != fOriginalWorkspacesRows
		|| brightness != fOriginalBrightness
		|| fSelected != fOriginal);
}


void
ScreenWindow::_UpdateOriginal()
{
	BPrivate::get_workspaces_layout(&fOriginalWorkspacesColumns,
		&fOriginalWorkspacesRows);

	fScreenMode.Get(fOriginal);
	fScreenMode.UpdateOriginalModes();
}


void
ScreenWindow::_UpdateMonitor()
{
	monitor_info info;
	float diagonalInches;
	status_t status = fScreenMode.GetMonitorInfo(info, &diagonalInches);
	if (status == B_OK) {
		char text[512];
		snprintf(text, sizeof(text), "%s%s%s %g\"", info.vendor,
			info.name[0] ? " " : "", info.name, diagonalInches);

		fScreenBox->SetLabel(text);
	} else {
		fScreenBox->SetLabel(B_TRANSLATE("Display info"));
	}

	// Add info about the graphics device

	accelerant_device_info deviceInfo;

	if (fScreenMode.GetDeviceInfo(deviceInfo) == B_OK) {
		BString deviceString;

		if (deviceInfo.name[0] && deviceInfo.chipset[0]) {
			deviceString.SetToFormat("%s (%s)", deviceInfo.name,
				deviceInfo.chipset);
		} else if (deviceInfo.name[0] || deviceInfo.chipset[0]) {
			deviceString
				= deviceInfo.name[0] ? deviceInfo.name : deviceInfo.chipset;
		}

		fDeviceInfo->SetText(deviceString);
	}


	char text[512];
	size_t length = 0;
	text[0] = 0;

	if (status == B_OK) {
		if (info.min_horizontal_frequency != 0
			&& info.min_vertical_frequency != 0
			&& info.max_pixel_clock != 0) {
			length = snprintf(text, sizeof(text),
				B_TRANSLATE("Horizonal frequency:\t%lu - %lu kHz\n"
					"Vertical frequency:\t%lu - %lu Hz\n\n"
					"Maximum pixel clock:\t%g MHz"),
				(long unsigned)info.min_horizontal_frequency,
				(long unsigned)info.max_horizontal_frequency,
				(long unsigned)info.min_vertical_frequency,
				(long unsigned)info.max_vertical_frequency,
				info.max_pixel_clock / 1000.0);
		}
		if (info.serial_number[0] && length < sizeof(text)) {
			if (length > 0) {
				text[length++] = '\n';
				text[length++] = '\n';
				text[length] = '\0';
			}
			length += snprintf(text + length, sizeof(text) - length,
				B_TRANSLATE("Serial no.: %s"), info.serial_number);
			if (info.produced.week != 0 && info.produced.year != 0
				&& length < sizeof(text)) {
				length += snprintf(text + length, sizeof(text) - length,
					" (%u/%u)", info.produced.week, info.produced.year);
			}
		}
	}

	if (text[0])
		fMonitorView->SetToolTip(text);
}


void
ScreenWindow::_UpdateColorLabel()
{
	BString string;
	string << fSelected.BitsPerPixel() << " " << B_TRANSLATE("bits/pixel");
	fColorsMenu->Superitem()->SetLabel(string.String());
}


void
ScreenWindow::_Apply()
{
	// make checkpoint, so we can undo these changes
	fUndoScreenMode.UpdateOriginalModes();

	status_t status = fScreenMode.Set(fSelected);
	if (status == B_OK) {
		// use the mode that has eventually been set and
		// thus we know to be working; it can differ from
		// the mode selected by user due to hardware limitation
		display_mode newMode;
		BScreen screen(this);
		screen.GetMode(&newMode);

		if (fAllWorkspacesItem->IsMarked()) {
			int32 originatingWorkspace = current_workspace();
			const int32 workspaceCount = count_workspaces();
			for (int32 i = 0; i < workspaceCount; i++) {
				if (i != originatingWorkspace)
					screen.SetMode(i, &newMode, true);
			}
			fBootWorkspaceApplied = true;
		} else {
			if (current_workspace() == 0)
				fBootWorkspaceApplied = true;
		}

		fActive = fSelected;

		// TODO: only show alert when this is an unknown mode
		BAlert* window = new AlertWindow(this);
		window->Go(NULL);
	} else {
		char message[256];
		snprintf(message, sizeof(message),
			B_TRANSLATE("The screen mode could not be set:\n\t%s\n"),
			screen_errors(status));
		BAlert* alert = new BAlert(B_TRANSLATE("Warning"), message,
			B_TRANSLATE("OK"), NULL, NULL,
			B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go();
	}
}
