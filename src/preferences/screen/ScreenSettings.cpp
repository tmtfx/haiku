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
#include <AppServerLink.h>
#include <ServerProtocol.h>


static const char* kSettingsFileName = "Screen_data";


static void
_WriteSettingsFile(const BPoint& offset, bool hardwareCursorEnabled)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) < B_OK)
		return;

	path.Append(kSettingsFileName);

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() == B_OK) {
		file.Write(&offset, sizeof(BPoint));
		file.Write(&hardwareCursorEnabled, sizeof(bool));
	}
}


ScreenSettings::ScreenSettings()
{
	fWindowFrame.Set(0, 0, 450, 250);
	BPoint offset;
	fHardwareCursorEnabled = true; // default

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
			}
		}
	}

	fWindowFrame.OffsetBy(offset);
}


ScreenSettings::~ScreenSettings()
{
	BPoint offset = fWindowFrame.LeftTop();
	_WriteSettingsFile(offset, fHardwareCursorEnabled);
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
	_WriteSettingsFile(offset, fHardwareCursorEnabled);
	// communicate with app_server
	BPrivate::AppServerLink link;
    link.StartMessage(AS_SET_HW_CUR_BITMAP_ENABLED);
    link.Attach<bool>(enabled);
    link.Flush();
}
