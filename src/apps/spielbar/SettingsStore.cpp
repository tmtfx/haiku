/*
 * SpielBar v3 - lettura e scrittura delle impostazioni
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Path.h>
#include <String.h>
#include <SupportDefs.h>
#include <stdio.h>
#include <string.h>

#include "SettingsStore.h"
#include "SpielBarTypes.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


bool
ViewSpecValid(int32 world, uint32 mask, int32 tags)
{
	return world >= kWorldCatalog && world <= kWorldSearch
		&& (mask & ~kKindFilterMask) == 0
		&& tags >= 0 && (uint32)tags <= kTagFilterMask;
}

void
SpecToMessage(BMessage& msg, const char* prefix, const ViewSpec& spec)
{
	BString k;
	k = prefix; k << "world"; msg.AddInt32(k.String(), (int32)spec.world);
	k = prefix; k << "mask"; msg.AddInt32(k.String(), (int32)spec.kindMask);
	k = prefix; k << "tag"; msg.AddInt32(k.String(), (int32)spec.tags);
}

bool
SpecFromMessage(const BMessage& msg, const char* prefix, ViewSpec* spec)
{
	BString k;
	int32 world, mask, tags;
	k = prefix; k << "world";
	if (msg.FindInt32(k.String(), &world) != B_OK)
		return false;
	k = prefix; k << "mask";
	if (msg.FindInt32(k.String(), &mask) != B_OK)
		return false;
	k = prefix; k << "tag";
	if (msg.FindInt32(k.String(), &tags) != B_OK)
		return false;
	if (!ViewSpecValid(world, (uint32)mask, tags))
		return false;
	spec->world = (World)world;
	spec->kindMask = (uint32)mask;
	spec->tags = (uint32)tags;
	return true;
}

status_t
SettingsPath(BPath* path)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, path, true);
	if (status != B_OK)
		return status;
	return path->Append("SpielBar");
}

void
SettingsLoad(Settings& settings)
{
	settings = Settings();

	BPath path;
	if (SettingsPath(&path) != B_OK)
		return;
	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;
	BMessage msg;
	if (msg.Unflatten(&file) != B_OK)
		return;

	bool b;
	if (msg.FindBool("group", &b) == B_OK)
		settings.options.groupByTag = b;
	if (msg.FindBool("hidesys", &b) == B_OK)
		settings.options.hideSystem = b;

	int32 mode;
	if (msg.FindInt32("startmode", &mode) == B_OK && mode >= kStartDefault
		&& mode <= kStartSaved)
		settings.startupMode = (StartupMode)mode;

	ViewSpec spec;
	if (msg.FindBool("hassaved", &b) == B_OK && b
		&& SpecFromMessage(msg, "saved_", &spec)) {
		settings.hasSavedView = true;
		settings.savedView = spec;
	}
	if (SpecFromMessage(msg, "last_", &spec))
		settings.lastView = spec;

	float units;
	if (msg.FindFloat("winh", &units) == B_OK && units >= 20.0f
		&& units <= 400.0f)
		settings.windowHeightUnits = units;

	if (settings.startupMode == kStartSaved && !settings.hasSavedView)
		settings.startupMode = kStartDefault;
}

void
SettingsSave(const Settings& settings)
{
	BPath path;
	if (SettingsPath(&path) != B_OK)
		return;
	BMessage msg;
	msg.AddBool("group", settings.options.groupByTag);
	msg.AddBool("hidesys", settings.options.hideSystem);
	msg.AddInt32("startmode", (int32)settings.startupMode);
	msg.AddBool("hassaved", settings.hasSavedView);
	SpecToMessage(msg, "saved_", settings.savedView);
	SpecToMessage(msg, "last_", settings.lastView);
	if (settings.windowHeightUnits > 0)
		msg.AddFloat("winh", settings.windowHeightUnits);

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t status = file.InitCheck();
	if (status == B_OK)
		status = msg.Flatten(&file);
	if (status != B_OK) {
		fprintf(stderr, "SpielBar: impossibile salvare %s: %s\n", path.Path(),
			strerror(status));
	}
}
