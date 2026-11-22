/*
 * SpielBar v3 - icone HVIF/MIME dei file
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#ifndef SPIELBAR_ICONSUPPORT_H
#define SPIELBAR_ICONSUPPORT_H

#include <Bitmap.h>
#include <Entry.h>
#include <MimeType.h>
#include <StorageDefs.h>
#include <SupportDefs.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


BBitmap*
LoadVectorIcon(int32 resourceID, float size = 32);

BBitmap*
MakeIconBitmap(int32 size);

icon_size
IconSizeFor(int32 size);

bool
TryGetIcon(BMimeType& mt, BBitmap* out, int32 size);

BBitmap*
LoadBestIconForRef(const entry_ref& ref, const char* signatureOpt, int32 size);

#endif	// SPIELBAR_ICONSUPPORT_H
