/*
 * Copyright 2005-2009, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */

/**	Manages all available physical screens */


#include "ScreenManager.h"

#include "Screen.h"
#include "ServerConfig.h"

#include "RemoteHWInterface.h"

#include <Autolock.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <String.h>
#include <StringList.h>
#include <SupportDefs.h>
#include <syslog.h>

#include <new>
#include <stdio.h>
#include <string.h>

using std::nothrow;


#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
#	include "AccelerantHWInterface.h"
#else
#	include "ViewHWInterface.h"
#	include "DWindowHWInterface.h"
#endif


ScreenManager* gScreenManager;


class ScreenChangeListener : public HWInterfaceListener {
public:
								ScreenChangeListener(ScreenManager& manager,
									Screen* screen);

private:
virtual	void					ScreenChanged(HWInterface* interface);

			ScreenManager&		fManager;
			Screen*				fScreen;
};


ScreenChangeListener::ScreenChangeListener(ScreenManager& manager,
	Screen* screen)
	:
	fManager(manager),
	fScreen(screen)
{
}


void
ScreenChangeListener::ScreenChanged(HWInterface* interface)
{
	fManager.ScreenChanged(fScreen);
}


ScreenManager::ScreenManager()
	:
	BLooper("screen manager"),
	fScreenList(4)
{
#ifdef HAIKU_TARGET_PLATFORM_LIBBE_TEST
#	if defined(USE_DIRECT_WINDOW_TEST_MODE)
	_AddHWInterface(new DWindowHWInterface());
#	else
	_AddHWInterface(new ViewHWInterface());
#	endif
#else
	_ScanDrivers();

	// turn on node monitoring the graphics driver directory
	BEntry entry("/dev/graphics");
	node_ref nodeRef;
	if (entry.InitCheck() == B_OK && entry.GetNodeRef(&nodeRef) == B_OK)
		watch_node(&nodeRef, B_WATCH_DIRECTORY, this);
#endif
}


ScreenManager::~ScreenManager()
{
}


Screen*
ScreenManager::ScreenAt(int32 index) const
{
	if (!IsLocked())
		debugger("Called ScreenManager::ScreenAt() without lock!");

	screen_item* item = fScreenList.ItemAt(index);
	if (item != NULL)
		return item->screen.Get();

	return NULL;
}


int32
ScreenManager::CountScreens() const
{
	if (!IsLocked())
		debugger("Called ScreenManager::CountScreens() without lock!");

	return fScreenList.CountItems();
}


status_t
ScreenManager::AcquireScreens(ScreenOwner* owner, int32* wishList,
	int32 wishCount, const char* target, bool force, ScreenList& list)
{
	BAutolock locker(this);
	int32 added = 0;

	// TODO: don't ignore the wish list

	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);

		if (item->owner == NULL && list.AddItem(item->screen.Get())) {
			item->owner = owner;
			added++;
		}
	}

	if (added == 0 && target != NULL) {
		// there's a specific target screen we want to initialize
		// TODO: right now we only support remote screens, but we could
		// also target specific accelerants to support other graphics cards
		HWInterface* interface;
#ifdef HAIKU_TARGET_PLATFORM_LIBBE_TEST
		interface = new(nothrow) ViewHWInterface();
#else
		interface = new(nothrow) RemoteHWInterface(target);
#endif
		if (interface != NULL) {
			screen_item* item = _AddHWInterface(interface);
			if (item != NULL && list.AddItem(item->screen.Get())) {
				item->owner = owner;
				added++;
			}
		}
	}

	return added > 0 ? B_OK : B_ENTRY_NOT_FOUND;
}


void
ScreenManager::ReleaseScreens(ScreenList& list)
{
	BAutolock locker(this);

	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);

		for (int32 j = 0; j < list.CountItems(); j++) {
			Screen* screen = list.ItemAt(j);

			if (item->screen.Get() == screen)
				item->owner = NULL;
		}
	}
}


void
ScreenManager::ScreenChanged(Screen* screen)
{
	BAutolock locker(this);

	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);
		if (item->screen.Get() == screen)
			item->owner->ScreenChanged(screen);
	}
}


#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
/*!	Recursively collects every graphics device path under \a directory,
	skipping the generic VESA/framebuffer fallback drivers (those are only
	used when no real card could be found at all, same as before multi-GPU
	support existed). Mirrors AccelerantHWInterface::_RecursiveScan(), but
	collects every match instead of stopping at a given index -- needed
	here so we know up front how many real cards exist and can open one
	AccelerantHWInterface per card.
*/
static void
collect_graphics_device_paths(const char* directory, BStringList& paths)
{
	BEntry entry;
	BDirectory dir(directory);
	while (dir.GetNextEntry(&entry) == B_OK) {
		BPath path;
		entry.GetPath(&path);
		if (!strcmp(path.Path(), "/dev/graphics/vesa")
			|| !strcmp(path.Path(), "/dev/graphics/framebuffer")) {
			continue;
		}

		if (entry.IsDirectory())
			collect_graphics_device_paths(path.Path(), paths);
		else
			paths.Add(path.Path());
	}
}
#endif


#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
// Declared (not static) in AccelerantHWInterface.cpp, no header prototype
// exists since it was only ever called from within that same file before.
extern bool use_fail_safe_video_mode();
#endif


void
ScreenManager::_ScanDrivers()
{
	// ToDo: to make monitoring the driver directory useful, we need more
	//	power and data here, and should do the scanning on our own

#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
	// Multi-GPU: respect the boot-time "fail-safe video mode" safemode
	// option exactly like the pre-multi-GPU single-card path always did
	// (AccelerantHWInterface::_OpenGraphicsDevice() checks the same flag)
	// -- collect_graphics_device_paths() below bypasses that check
	// entirely since it does its own directory walk and opens explicit
	// paths, so without this, requesting fail-safe video at boot was
	// silently ignored for any secondary/real driver (confirmed on real
	// hardware: sm750 kept loading instead of falling back to VESA).
	if (!use_fail_safe_video_mode()) {
		BStringList devicePaths;
		collect_graphics_device_paths("/dev/graphics/", devicePaths);

		// Multi-GPU: app_server normally logs nothing to syslog at all
		// (every ATRACE/STRACE call is compiled out in release builds),
		// which made this code path impossible to diagnose from a real
		// hardware test's syslog -- log unconditionally via syslog()
		// (not compiled out) until this has seen enough real multi-GPU
		// hardware to be trusted blind.
		syslog(LOG_INFO, "Multi-GPU: found %" B_PRId32 " graphics device(s)"
			" under /dev/graphics/", devicePaths.CountStrings());
		for (int32 i = 0; i < devicePaths.CountStrings(); i++) {
			syslog(LOG_INFO, "Multi-GPU:   [%" B_PRId32 "] %s", i,
				devicePaths.StringAt(i).String());
		}

		if (devicePaths.CountStrings() > 0) {
			// One AccelerantHWInterface per distinct card. Fase D/E
			// (Desktop's activeGpuList) decides later which of these
			// actually get used for a workspace row, based on which
			// ones have a usable EDID.
			for (int32 i = 0; i < devicePaths.CountStrings(); i++) {
				syslog(LOG_INFO, "Multi-GPU: opening [%" B_PRId32 "] %s ...",
					i, devicePaths.StringAt(i).String());
				screen_item* item = _AddHWInterface(new AccelerantHWInterface(),
					devicePaths.StringAt(i).String());
				syslog(LOG_INFO, "Multi-GPU: opening [%" B_PRId32 "] %s: %s",
					i, devicePaths.StringAt(i).String(),
					item != NULL ? "OK" : "FAILED");
			}
			return;
		}
	} else {
		syslog(LOG_INFO, "Multi-GPU: fail-safe video mode requested, "
			"skipping real-driver enumeration entirely");
	}

	// Fail-safe video mode requested, or no real card found -- fall back
	// to exactly the old single-GPU behavior (AccelerantHWInterface
	// itself falls back further to VESA/framebuffer either way).
	_AddHWInterface(new AccelerantHWInterface());
#endif
}


#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
/*!	Applies this user's stored hardware-cursor/2D-acceleration/cursor-
	animation preference for screen index \a screenIndex, read from
	Screen_data (now a flattened BMessage, keyed per screen index -- see
	ScreenSettings.cpp's _Key() and MULTIGPU_PLAN.md Fase F). Called right
	after a Screen successfully wraps \a interface, i.e. as soon as its
	final screen index is known (AccelerantHWInterface::Initialize() runs
	too early for this -- it has no way to know what index it's about to
	be assigned).
*/
static BString
settings_key(const char* base, const BString& key)
{
	BString result(base);
	result << key;
	return result;
}


/*!	The key ScreenSettings' per-GPU preferences persist under for this
	screen -- its PCI BDF if resolvable, else a fallback "idx<N>" that at
	least works within a single boot session (cards with no resolvable
	PCI location, e.g. virtio, have no better stable identity available
	-- see MULTIGPU_PLAN.md EDID section). Duplicated near-identically in
	ScreenWindow.cpp's and GPUSettingsView.cpp's own gpu_settings_key().
*/
static BString
gpu_settings_key(AccelerantHWInterface* accelerant, int32 screenIndex)
{
	uint8 bus, device, function;
	if (accelerant != NULL
		&& accelerant->GetPciLocation(&bus, &device, &function) == B_OK) {
		char bdf[7];
		snprintf(bdf, sizeof(bdf), "%02x%02x%02x", bus, device, function);
		return bdf;
	}

	BString key("idx");
	key << screenIndex;
	return key;
}


static void
apply_user_graphics_settings(HWInterface* interface, int32 screenIndex)
{
	AccelerantHWInterface* accelerant
		= dynamic_cast<AccelerantHWInterface*>(interface);
	if (accelerant == NULL)
		return;

	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;

	path.Append("Screen_data");
	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;

	BMessage settings;
	if (settings.Unflatten(&file) != B_OK)
		return;

	BString key = gpu_settings_key(accelerant, screenIndex);

	bool hw = true;
	if (settings.FindBool(settings_key("hwcursor", key), &hw) == B_OK)
		accelerant->SetUserHardwareCursor(hw);

	bool acc = false;
	if (settings.FindBool(settings_key("accel2d", key), &acc) == B_OK)
		accelerant->SetUser2DAcceleration(acc);

	bool dbg = false;
	if (settings.FindBool(settings_key("debug2d", key), &dbg) == B_OK)
		accelerant->SetUserDebugLed(dbg);

	bool cursorAnimation = false;
	settings.FindBool(settings_key("cursorani", key), &cursorAnimation);
	int32 animationSpeed = 100;
	settings.FindInt32(settings_key("cursoranispeed", key),
		&animationSpeed);
	int32 animationIdleDelayMs = 0;
	settings.FindInt32(settings_key("cursoranidelay", key),
		&animationIdleDelayMs);
	accelerant->SetUserCursorAnimationSettings(cursorAnimation,
		max_c(0.1f, animationSpeed / 100.0f),
		(uint32)max_c((int32)0, animationIdleDelayMs));
}
#endif


ScreenManager::screen_item*
ScreenManager::_AddHWInterface(HWInterface* interface, const char* devicePath)
{
	ObjectDeleter<Screen> screen(
		new(nothrow) Screen(interface, fScreenList.CountItems()));
	if (!screen.IsSet()) {
		delete interface;
		return NULL;
	}

	// The interface is now owned by the screen

	syslog(LOG_INFO, "Multi-GPU: Screen::Initialize(%s) starting...",
		devicePath != NULL ? devicePath : "(scan)");
	status_t initStatus = screen->Initialize(devicePath);
	syslog(LOG_INFO, "Multi-GPU: Screen::Initialize(%s) returned %" B_PRId32,
		devicePath != NULL ? devicePath : "(scan)", (int32)initStatus);

	if (initStatus >= B_OK) {
#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
		apply_user_graphics_settings(interface, screen->ID());
#endif

		screen_item* item = new(nothrow) screen_item;

		if (item != NULL) {
			item->screen.SetTo(screen.Detach());
			item->owner = NULL;
			item->listener.SetTo(
				new(nothrow) ScreenChangeListener(*this, item->screen.Get()));
			if (item->listener.IsSet()
				&& interface->AddListener(item->listener.Get())) {
				if (fScreenList.AddItem(item))
					return item;

				interface->RemoveListener(item->listener.Get());
			}

			delete item;
		}
	}

	return NULL;
}


void
ScreenManager::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_NODE_MONITOR:
			// TODO: handle notification
			break;

		default:
			BHandler::MessageReceived(message);
	}
}
