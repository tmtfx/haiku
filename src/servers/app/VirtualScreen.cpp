/*
 * Copyright 2005-2013, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "VirtualScreen.h"

#include "HWInterface.h"
#include "Desktop.h"

#include <new>


VirtualScreen::VirtualScreen()
	:
	fScreenList(4),
	fDrawingEngine(NULL),
	fHWInterface(NULL)
{
}


VirtualScreen::~VirtualScreen()
{
	_Reset();
}


void
VirtualScreen::_Reset()
{
	ScreenList list;
	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);

		list.AddItem(item->screen);
	}

	gScreenManager->ReleaseScreens(list);
	fScreenList.MakeEmpty();

	fFrame.Set(0, 0, 0, 0);
	fDrawingEngine = NULL;
	fHWInterface = NULL;
}


status_t
VirtualScreen::SetConfiguration(Desktop& desktop, const ScreenConfigurations& configurations,
	ScreenConfigurations& currentConfigurations, uint32* _changedScreens,
	Screen* onlyReconfigure)
{
	// Remember previous screen modes

	typedef std::map<Screen*, display_mode> ScreenModeMap;
	ScreenModeMap previousModes;
	bool previousModesFailed = false;

	if (_changedScreens != NULL) {
		*_changedScreens = 0;

		try {
			for (int32 i = 0; i < fScreenList.CountItems(); i++) {
				Screen* screen = fScreenList.ItemAt(i)->screen;

				display_mode mode;
				screen->GetMode(mode);

				previousModes.insert(std::make_pair(screen, mode));
			}
		} catch (...) {
			previousModesFailed = true;
			*_changedScreens = ~0L;
		}
	}

	_Reset();

	ScreenList list;
	status_t status = gScreenManager->AcquireScreens(&desktop, NULL, 0,
		desktop.TargetScreen(), false, list);
	if (status != B_OK) {
		// TODO: we would try again here with force == true
		return status;
	}

	for (int32 i = 0; i < list.CountItems(); i++) {
		Screen* screen = list.ItemAt(i);

		// Multi-GPU: when the caller only actually cares about one
		// specific screen (a workspace switch, or any other targeted
		// reconfiguration -- never the very first, whole-desktop setup,
		// which still passes onlyReconfigure == NULL and reconfigures
		// everyone as before), leave every OTHER screen's hardware
		// completely untouched instead of unconditionally re-running
		// SetMode()/SetPreferredMode()/SetBestMode() on it too. Ported
		// from the old MultiGPU branch (commit 82289e7a47): on real
		// multi-GPU hardware sharing a bus/bridge, re-driving a screen
		// that isn't even changing is wasteful at best and a plausible
		// source of visible disturbance to whatever screen actually IS
		// changing at worst.
		bool touchHardware = onlyReconfigure == NULL || screen == onlyReconfigure;
		AddScreen(screen, configurations, currentConfigurations, touchHardware);

		if (!previousModesFailed && _changedScreens != NULL) {
			// Figure out which screens have changed their mode
			display_mode mode;
			screen->GetMode(mode);

			ScreenModeMap::const_iterator found = previousModes.find(screen);
			if (found != previousModes.end()
				&& memcmp(&mode, &found->second, sizeof(display_mode)))
				*_changedScreens |= 1 << i;
		}
	}

	UpdateFrame();
	return B_OK;
}


status_t
VirtualScreen::AddScreen(Screen* screen, const ScreenConfigurations& configurations,
	ScreenConfigurations& currentConfigurations, bool touchHardware)
{
	screen_item* item = new(std::nothrow) screen_item;
	if (item == NULL)
		return B_NO_MEMORY;

	item->screen = screen;

	display_mode mode;

	if (!touchHardware) {
		// Multi-GPU: this screen isn't the one this reconfiguration is
		// actually targeting -- just register it with whatever mode it's
		// already running, without issuing a single SetMode()/
		// SetPreferredMode()/SetBestMode() hardware call. See the caller
		// in SetConfiguration() for why.
		screen->GetMode(mode);
	} else {
		status_t status = B_ERROR;
		if (_GetMode(screen, configurations, mode) == B_OK) {
			// we found settings for this screen, and try to apply them now
			status = screen->SetMode(mode);
		}

		if (status != B_OK) {
			// We found no configuration or it wasn't valid, try to fallback
			// to sane values
			status = screen->SetPreferredMode();
			if (status == B_OK) {
				screen->GetMode(mode);
			}
			if (status != B_OK)
				status = screen->SetBestMode(1024, 768, B_RGB32, 60.f);
			if (status != B_OK)
				status = screen->SetBestMode(800, 600, B_RGB32, 60.f, false);
			if (status != B_OK) {
				debug_printf("app_server: Failed to set mode: %s\n",
					strerror(status));

				delete item;
				return status;
			}
		}
	}

	monitor_info info;
	bool hasInfo = screen->GetMonitorInfo(info) == B_OK;
	currentConfigurations.Set(screen->ID(), hasInfo ? &info : NULL, screen->Frame(), mode);

	// TODO: this works only for single screen configurations
	fDrawingEngine = screen->GetDrawingEngine();
	fHWInterface = screen->HWInterface();
	fFrame = screen->Frame();
	item->frame = fFrame;

	fScreenList.AddItem(item);

	return B_OK;
}


void
VirtualScreen::SetPrimaryInterface(Screen* screen)
{
	// Multi-GPU: see the comment on HWInterface() in the header -- this
	// does not make fHWInterface/fDrawingEngine multiplex per caller,
	// it just corrects which ONE screen they consistently point to
	// (the primary, per Desktop::ActiveGpuScreens(), instead of
	// whichever AddScreen() call happened to run last).
	if (screen == NULL)
		return;

	fDrawingEngine = screen->GetDrawingEngine();
	fHWInterface = screen->HWInterface();
}


status_t
VirtualScreen::RemoveScreen(Screen* screen)
{
	// not implemented yet (config changes when running)
	return B_ERROR;
}


void
VirtualScreen::UpdateFrame()
{
	int32 virtualWidth = 0, virtualHeight = 0;

	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		Screen* screen = fScreenList.ItemAt(i)->screen;

		uint16 width, height;
		uint32 colorSpace;
		float frequency;
		screen->GetMode(width, height, colorSpace, frequency);

		// TODO: compute virtual size depending on the actual screen position!
		virtualWidth += width;
		virtualHeight = max_c(virtualHeight, height);
	}

	fFrame.Set(0, 0, virtualWidth - 1, virtualHeight - 1);
}


/*!	Returns the smallest frame that spans over all screens
*/
BRect
VirtualScreen::Frame() const
{
	return fFrame;
}


Screen*
VirtualScreen::ScreenAt(int32 index) const
{
	screen_item* item = fScreenList.ItemAt(index);
	if (item != NULL)
		return item->screen;

	return NULL;
}


Screen*
VirtualScreen::ScreenByID(int32 id) const
{
	for (int32 i = fScreenList.CountItems(); i-- > 0;) {
		screen_item* item = fScreenList.ItemAt(i);

		if (item->screen->ID() == id || id == B_MAIN_SCREEN_ID.id)
			return item->screen;
	}

	return NULL;
}


BRect
VirtualScreen::ScreenFrameAt(int32 index) const
{
	screen_item* item = fScreenList.ItemAt(index);
	if (item != NULL)
		return item->frame;

	return BRect(0, 0, 0, 0);
}


int32
VirtualScreen::CountScreens() const
{
	return fScreenList.CountItems();
}


status_t
VirtualScreen::_GetMode(Screen* screen, const ScreenConfigurations& configurations,
	display_mode& mode) const
{
	monitor_info info;
	bool hasInfo = screen->GetMonitorInfo(info) == B_OK;

	screen_configuration* configuration = configurations.BestFit(screen->ID(),
		hasInfo ? &info : NULL);
	if (configuration == NULL)
		return B_NAME_NOT_FOUND;

	mode = configuration->mode;
	return B_OK;
}
