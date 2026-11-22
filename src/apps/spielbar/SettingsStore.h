/*
 * SpielBar v3 - lettura e scrittura delle impostazioni
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#ifndef SPIELBAR_SETTINGSSTORE_H
#define SPIELBAR_SETTINGSSTORE_H

#include <Message.h>
#include <Path.h>
#include <SupportDefs.h>

#include "SpielBarTypes.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


bool
ViewSpecValid(int32 world, uint32 mask, int32 tags);

void
SpecToMessage(BMessage& msg, const char* prefix, const ViewSpec& spec);

bool
SpecFromMessage(const BMessage& msg, const char* prefix, ViewSpec* spec);

status_t
SettingsPath(BPath* path);

// Alla lettura si valida tutto: i valori invalidi diventano predefiniti.

void
SettingsLoad(Settings& settings);

void
SettingsSave(const Settings& settings);

#endif	// SPIELBAR_SETTINGSSTORE_H
