/*
 * Copyright 2001-2015, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Rafael Romo
 *		Stefano Ceccherini (burton666@libero.it)
 *		Axel Dörfler, axeld@pinc-software.de
 *		Fabio Tomat, f.t.public@gmail.com
 */


#include "ScreenSettings.h"

#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Messenger.h>
#include <String.h>

#include "ServerProtocol.h"


static const char* kSettingsFileName = "Screen_data";


BString
ScreenSettings::_Key(const char* base, int32 screen)
{
	BString key(base);
	key << screen;
	return key;
}


ScreenSettings::ScreenSettings()
	:
	fCompositor2DEnabled(false)
{
	fWindowFrame.Set(0, 0, 450, 250);
	_Load();
}


ScreenSettings::~ScreenSettings()
{
	_Save();
}


void
ScreenSettings::_Load()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;

	path.Append(kSettingsFileName);

	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;

	BMessage stored;
	if (stored.Unflatten(&file) != B_OK)
		return;

	BPoint offset;
	if (stored.FindPoint("window_offset", &offset) == B_OK)
		fWindowFrame.OffsetBy(offset);

	bool compositor = false;
	if (stored.FindBool("compositor2d", &compositor) == B_OK)
		fCompositor2DEnabled = compositor;

	fPerScreen = stored;
		// per-screen keys (hwcursor0, accel2d0, ...) live right alongside
		// the two globals above; harmless to keep them all in one message
		// since _Key() names never collide with "window_offset"/
		// "compositor2d".
}


void
ScreenSettings::_Save() const
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;

	path.Append(kSettingsFileName);

	BMessage toStore(fPerScreen);
	toStore.RemoveName("window_offset");
	toStore.RemoveName("compositor2d");
	toStore.AddPoint("window_offset", fWindowFrame.LeftTop());
	toStore.AddBool("compositor2d", fCompositor2DEnabled);

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() == B_OK)
		toStore.Flatten(&file);
}


void
ScreenSettings::SetWindowFrame(BRect frame)
{
	fWindowFrame = frame;
	_Save();
}


void
ScreenSettings::SetCompositor2DEnabled(bool enabled)
{
	fCompositor2DEnabled = enabled;
	_Save();

	BMessage msg(AS_SET_2D_COMPOSITOR_ENABLED);
	msg.AddBool("enable", enabled);
	BMessenger appServerMessenger("application/x-vnd.Haiku-app_server");
	appServerMessenger.SendMessage(&msg);
}


BStringList
ScreenSettings::GpuOrder() const
{
	BStringList order;
	BString bdf;
	for (int32 i = 0; fPerScreen.FindString("gpu_order", i, &bdf) == B_OK; i++)
		order.Add(bdf);
	return order;
}


void
ScreenSettings::SetGpuOrder(const BStringList& order)
{
	fPerScreen.RemoveName("gpu_order");
	for (int32 i = 0; i < order.CountStrings(); i++)
		fPerScreen.AddString("gpu_order", order.StringAt(i));
	_Save();

	BMessage msg(AS_GPU_CONFIG_CHANGED);
	BMessenger appServerMessenger("application/x-vnd.Haiku-app_server");
	appServerMessenger.SendMessage(&msg);
}


static BString
edid_unset_key(const BString& bdf)
{
	BString key("edidunset_");
	key << bdf;
	return key;
}


bool
ScreenSettings::EdidUnset(const BString& bdf) const
{
	bool unset = false;
	fPerScreen.FindBool(edid_unset_key(bdf), &unset);
	return unset;
}


void
ScreenSettings::SetEdidUnset(const BString& bdf, bool unset)
{
	BString key = edid_unset_key(bdf);
	fPerScreen.RemoveName(key);
	fPerScreen.AddBool(key, unset);
	_Save();

	BMessage msg(AS_GPU_CONFIG_CHANGED);
	BMessenger appServerMessenger("application/x-vnd.Haiku-app_server");
	appServerMessenger.SendMessage(&msg);
}


bool
ScreenSettings::HardwareCursorEnabled(int32 screen) const
{
	bool enabled = false;
	fPerScreen.FindBool(_Key("hwcursor", screen), &enabled);
	return enabled;
}


void
ScreenSettings::SetHardwareCursorEnabled(int32 screen, bool enabled)
{
	BString key = _Key("hwcursor", screen);
	fPerScreen.RemoveName(key);
	fPerScreen.AddBool(key, enabled);
	_Save();

	BMessage msg(AS_SET_HW_CUR_BITMAP_ENABLED);
	msg.AddBool("enable", enabled);
	msg.AddInt32("screen", screen);
	BMessenger appServerMessenger("application/x-vnd.Haiku-app_server");
	appServerMessenger.SendMessage(&msg);
}


bool
ScreenSettings::Acceleration2DEnabled(int32 screen) const
{
	bool enabled = false;
	fPerScreen.FindBool(_Key("accel2d", screen), &enabled);
	return enabled;
}


void
ScreenSettings::SetAcceleration2DEnabled(int32 screen, bool enabled)
{
	BString key = _Key("accel2d", screen);
	fPerScreen.RemoveName(key);
	fPerScreen.AddBool(key, enabled);
	_Save();

	BMessage msg(AS_SET_2D_ACCELERATION_ENABLED);
	msg.AddBool("enable", enabled);
	msg.AddInt32("screen", screen);
	BMessenger appServerMessenger("application/x-vnd.Haiku-app_server");
	appServerMessenger.SendMessage(&msg);
}


bool
ScreenSettings::Visual2DDebugEnabled(int32 screen) const
{
	bool enabled = false;
	fPerScreen.FindBool(_Key("debug2d", screen), &enabled);
	return enabled;
}


void
ScreenSettings::SetVisual2DDebugEnabled(int32 screen, bool enabled)
{
	BString key = _Key("debug2d", screen);
	fPerScreen.RemoveName(key);
	fPerScreen.AddBool(key, enabled);
	_Save();

	BMessage msg(AS_SET_2D_DEBUG_ENABLED);
	msg.AddBool("enable", enabled);
	msg.AddInt32("screen", screen);
	BMessenger appServerMessenger("application/x-vnd.Haiku-app_server");
	appServerMessenger.SendMessage(&msg);
}


bool
ScreenSettings::CursorAnimationEnabled(int32 screen) const
{
	bool enabled = false;
	fPerScreen.FindBool(_Key("cursorani", screen), &enabled);
	return enabled;
}


int32
ScreenSettings::CursorAnimationSpeedPercent(int32 screen) const
{
	int32 percent = 100;
	fPerScreen.FindInt32(_Key("cursoranispeed", screen), &percent);
	return percent;
}


int32
ScreenSettings::CursorAnimationIdleDelayMs(int32 screen) const
{
	int32 delay = 0;
	fPerScreen.FindInt32(_Key("cursoranidelay", screen), &delay);
	return delay;
}


static void
_SendCursorAnimationSettings(int32 screen, bool enabled, int32 speedPercent,
	int32 idleDelayMs)
{
	BMessage msg(AS_SET_CURSOR_ANI_SETTINGS);
	msg.AddBool("enable", enabled);
	msg.AddInt32("speed_percent", speedPercent);
	msg.AddInt32("idle_delay_ms", idleDelayMs);
	msg.AddInt32("screen", screen);
	BMessenger appServerMessenger("application/x-vnd.Haiku-app_server");
	appServerMessenger.SendMessage(&msg);
}


void
ScreenSettings::SetCursorAnimationEnabled(int32 screen, bool enabled)
{
	BString key = _Key("cursorani", screen);
	fPerScreen.RemoveName(key);
	fPerScreen.AddBool(key, enabled);
	_Save();
	_SendCursorAnimationSettings(screen, enabled,
		CursorAnimationSpeedPercent(screen), CursorAnimationIdleDelayMs(screen));
}


void
ScreenSettings::SetCursorAnimationSpeedPercent(int32 screen, int32 percent)
{
	if (percent < 10)
		percent = 10;
	BString key = _Key("cursoranispeed", screen);
	fPerScreen.RemoveName(key);
	fPerScreen.AddInt32(key, percent);
	_Save();
	_SendCursorAnimationSettings(screen, CursorAnimationEnabled(screen), percent,
		CursorAnimationIdleDelayMs(screen));
}


void
ScreenSettings::SetCursorAnimationIdleDelayMs(int32 screen, int32 delayMs)
{
	if (delayMs < 0)
		delayMs = 0;
	BString key = _Key("cursoranidelay", screen);
	fPerScreen.RemoveName(key);
	fPerScreen.AddInt32(key, delayMs);
	_Save();
	_SendCursorAnimationSettings(screen, CursorAnimationEnabled(screen),
		CursorAnimationSpeedPercent(screen), delayMs);
}
