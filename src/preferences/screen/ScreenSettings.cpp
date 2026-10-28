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
#include <Message.h>
#include <Messenger.h>
#include "ServerProtocol.h"


static const char* kSettingsFileName = "Screen_data";


static void
_WriteSettingsFile(const BPoint& offset, bool hardwareCursorEnabled, bool acceleration2DEnabled,
	bool compositor2DEnabled, bool visual2DDebugEnabled, int32 displayRotation)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) < B_OK)
		return;

	path.Append(kSettingsFileName);

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() == B_OK) {
		file.Write(&offset, sizeof(BPoint));
		file.Write(&hardwareCursorEnabled, sizeof(bool));
		file.Write(&acceleration2DEnabled, sizeof(bool));
		file.Write(&compositor2DEnabled, sizeof(bool));
		file.Write(&visual2DDebugEnabled, sizeof(bool));
		file.Write(&displayRotation, sizeof(int32));
	}
}


ScreenSettings::ScreenSettings()
{
	fWindowFrame.Set(0, 0, 450, 250);
	BPoint offset;
	fHardwareCursorEnabled = false; // default
	fAcceleration2DEnabled = false; // default
	fCompositor2DEnabled = false; // default
	fVisual2DDebugEnabled = false; // default
	fDisplayRotation = 0; // default: no rotation

	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
		path.Append(kSettingsFileName);

		BFile file(path.Path(), B_READ_ONLY);
		if (file.InitCheck() == B_OK) {
			// read offset if present
			ssize_t read = file.Read(&offset, sizeof(BPoint));
			if (read == (ssize_t)sizeof(BPoint)) {
				// try to read saved hardware cursor flag
				bool hw = true;
				ssize_t read2 = file.Read(&hw, sizeof(bool));
				if (read2 == (ssize_t)sizeof(bool))
					fHardwareCursorEnabled = hw;

				// try to read saved 2D acceleration flag
				bool acc = false;
				ssize_t read3 = file.Read(&acc, sizeof(bool));
				if (read3 == (ssize_t)sizeof(bool))
					fAcceleration2DEnabled = acc;

				// try to read saved 2D compositor flag
				bool comp = false;
				ssize_t read4 = file.Read(&comp, sizeof(bool));
				if (read4 == (ssize_t)sizeof(bool))
					fCompositor2DEnabled = comp;
				
				// try to read saved 2D visual debug flag
				bool dbg = false;
				ssize_t read5 = file.Read(&dbg, sizeof(bool));
				if (read5 == (ssize_t)sizeof(bool))
					fVisual2DDebugEnabled = dbg;

				// try to read saved display rotation (0, 90, 180, 270)
				int32 rot = 0;
				ssize_t read6 = file.Read(&rot, sizeof(int32));
				if (read6 == (ssize_t)sizeof(int32))
					fDisplayRotation = rot;
			}
		}
	}

	fWindowFrame.OffsetBy(offset);
}


ScreenSettings::~ScreenSettings()
{
	BPoint offset = fWindowFrame.LeftTop();
	_WriteSettingsFile(offset, fHardwareCursorEnabled, fAcceleration2DEnabled, fCompositor2DEnabled, fVisual2DDebugEnabled, fDisplayRotation);
}


void
ScreenSettings::SetWindowFrame(BRect frame)
{
	fWindowFrame = frame;
}


void
ScreenSettings::SetHardwareCursorEnabled(bool enabled)
{
	fHardwareCursorEnabled = enabled;
	// persist immediately
	BPoint offset = fWindowFrame.LeftTop();
	_WriteSettingsFile(offset, fHardwareCursorEnabled, fAcceleration2DEnabled, fCompositor2DEnabled, fVisual2DDebugEnabled, fDisplayRotation);
	// communicate with app_server
	BMessage msg(AS_SET_HW_CUR_BITMAP_ENABLED);
	msg.AddBool("enable", enabled);
	BMessenger appServerMessenger("application/x-vnd.Haiku-app_server");
	appServerMessenger.SendMessage(&msg);
}


void
ScreenSettings::SetAcceleration2DEnabled(bool enabled)
{
	fAcceleration2DEnabled = enabled;
	// persist immediately
	BPoint offset = fWindowFrame.LeftTop();
	_WriteSettingsFile(offset, fHardwareCursorEnabled, fAcceleration2DEnabled, fCompositor2DEnabled, fVisual2DDebugEnabled, fDisplayRotation);
	// communicate with app_server
	BMessage msg(AS_SET_2D_ACCELERATION_ENABLED);
	msg.AddBool("enable", enabled);
	BMessenger appServerMessenger("application/x-vnd.Haiku-app_server");
	appServerMessenger.SendMessage(&msg);
}


void
ScreenSettings::SetCompositor2DEnabled(bool enabled)
{
	fCompositor2DEnabled = enabled;
	// persist immediately
	BPoint offset = fWindowFrame.LeftTop();
	_WriteSettingsFile(offset, fHardwareCursorEnabled, fAcceleration2DEnabled, fCompositor2DEnabled, fVisual2DDebugEnabled, fDisplayRotation);
	// communicate with app_server
	BMessage msg(AS_SET_2D_COMPOSITOR_ENABLED);
	msg.AddBool("enable", enabled);
	BMessenger appServerMessenger("application/x-vnd.Haiku-app_server");
	appServerMessenger.SendMessage(&msg);
}

void
ScreenSettings::SetVisual2DDebugEnabled(bool enabled)
{
    fVisual2DDebugEnabled = enabled;
    // persist immediately
    BPoint offset = fWindowFrame.LeftTop();
    _WriteSettingsFile(offset, fHardwareCursorEnabled, fAcceleration2DEnabled, fCompositor2DEnabled, fVisual2DDebugEnabled, fDisplayRotation);
    // communicate with app_server
    BMessage msg(AS_SET_2D_DEBUG_ENABLED);
    msg.AddBool("enable", enabled);
    BMessenger appServerMessenger("application/x-vnd.Haiku-app_server");
    appServerMessenger.SendMessage(&msg);
}


void
ScreenSettings::SetDisplayRotation(int32 degrees)
{
	// Validate: only 0, 90, 180, 270 are allowed
	if (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270)
		return;

	fDisplayRotation = degrees;
	// persist immediately
	BPoint offset = fWindowFrame.LeftTop();
	_WriteSettingsFile(offset, fHardwareCursorEnabled, fAcceleration2DEnabled, fCompositor2DEnabled, fVisual2DDebugEnabled, fDisplayRotation);
	// communicate with app_server
	BMessage msg(AS_SET_SCREEN_ROTATION);
	msg.AddInt32("rotation", degrees);
	BMessenger appServerMessenger("application/x-vnd.Haiku-app_server");
	appServerMessenger.SendMessage(&msg);
}
