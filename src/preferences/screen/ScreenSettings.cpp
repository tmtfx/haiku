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
	bool compositor2DEnabled)
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
	}
}


ScreenSettings::ScreenSettings()
{
	fWindowFrame.Set(0, 0, 450, 250);
	BPoint offset;
	fHardwareCursorEnabled = true; // default
	fAcceleration2DEnabled = false; // default
	fCompositor2DEnabled = false; // default

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
			}
		}
	}

	fWindowFrame.OffsetBy(offset);
}


ScreenSettings::~ScreenSettings()
{
	BPoint offset = fWindowFrame.LeftTop();
	_WriteSettingsFile(offset, fHardwareCursorEnabled, fAcceleration2DEnabled, fCompositor2DEnabled);
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
	_WriteSettingsFile(offset, fHardwareCursorEnabled, fAcceleration2DEnabled, fCompositor2DEnabled);
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
	_WriteSettingsFile(offset, fHardwareCursorEnabled, fAcceleration2DEnabled, fCompositor2DEnabled);
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
	_WriteSettingsFile(offset, fHardwareCursorEnabled, fAcceleration2DEnabled, fCompositor2DEnabled);
	// communicate with app_server
	BMessage msg(AS_SET_2D_COMPOSITOR_ENABLED);
	msg.AddBool("enable", enabled);
	BMessenger appServerMessenger("application/x-vnd.Haiku-app_server");
	appServerMessenger.SendMessage(&msg);
}
