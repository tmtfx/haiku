/*
 * SpielBar v3 - icone HVIF/MIME dei file
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#include <Application.h>
#include <Bitmap.h>
#include <Entry.h>
#include <File.h>
#include <IconUtils.h>
#include <MimeType.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Rect.h>
#include <Resources.h>
#include <Roster.h>
#include <StorageDefs.h>
#include <SupportDefs.h>
#include <string.h>

#include "IconSupport.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


BBitmap*
LoadVectorIcon(int32 resourceID, float size )
{
	app_info appInfo;
	if (be_app->GetAppInfo(&appInfo) != B_OK)
		return NULL;

	BFile file(&appInfo.ref, B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return NULL;

	BResources res;
	if (res.SetTo(&file) != B_OK)
		return NULL;

	size_t len;
	const void* data = res.LoadResource(B_VECTOR_ICON_TYPE, resourceID, &len);
	if (!data)
		return NULL;

	BBitmap* bmp = new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32);
	if (BIconUtils::GetVectorIcon((const uint8*)data, len, bmp) != B_OK) {
		delete bmp;
		return NULL;
	}
	return bmp;
}

BBitmap*
MakeIconBitmap(int32 size)
{
	return new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32);
}

icon_size
IconSizeFor(int32 size)
{
	return (size <= 16) ? B_MINI_ICON : B_LARGE_ICON;
}

bool
TryGetIcon(BMimeType& mt, BBitmap* out, int32 size)
{
	if (!mt.IsInstalled())
		return false;

	// Prima l'icona vettoriale: si rasterizza a qualsiasi dimensione.
	uint8* data = NULL;
	size_t dataSize = 0;
	if (mt.GetIcon(&data, &dataSize) == B_OK && data != NULL) {
		status_t status = BIconUtils::GetVectorIcon(data, dataSize, out);
		delete[] data;
		if (status == B_OK)
			return true;
	}

	// Le bitmap classiche esistono solo a 16 e a 32 px.
	if (size != 16 && size != 32)
		return false;

	BBitmap* tmp = MakeIconBitmap(size);
	bool ok = false;
	if (mt.GetIcon(tmp, IconSizeFor(size)) == B_OK) {
		memcpy(out->Bits(), tmp->Bits(), tmp->BitsLength());
		ok = true;
	}
	delete tmp;
	return ok;
}

BBitmap*
LoadBestIconForRef(const entry_ref& ref, const char* signatureOpt, int32 size)
{
	BBitmap* outIcon = MakeIconBitmap(size);
	bool loaded = false;

	{
		BNode node(&ref);
		if (node.InitCheck() == B_OK) {
			if (BIconUtils::GetVectorIcon(&node, "BEOS:ICON", outIcon) == B_OK)
				loaded = true;
		}
	}

	if (!loaded) {
		char mimeType[B_MIME_TYPE_LENGTH] = {0};
		if (signatureOpt && signatureOpt[0]) {
			strlcpy(mimeType, signatureOpt, sizeof(mimeType));
		} else {
			BNode node(&ref);
			BNodeInfo nodeInfo(&node);
			nodeInfo.GetType(mimeType);
		}

		if (mimeType[0] != '\0') {
			BMimeType mt(mimeType);
			if (TryGetIcon(mt, outIcon, size)) {
				loaded = true;
			} else {
				BMimeType super;
				if (mt.GetSupertype(&super) == B_OK) {
					if (TryGetIcon(super, outIcon, size))
						loaded = true;
				}
			}
		} else {
			BEntry entry(&ref, true);
			if (entry.InitCheck() == B_OK) {
				if (entry.IsDirectory()) {
					BMimeType folderType("application/x-vnd.Be-directory");
					if (TryGetIcon(folderType, outIcon, size))
						loaded = true;
				} else {
					BMimeType fileType("application/octet-stream");
					if (TryGetIcon(fileType, outIcon, size))
						loaded = true;
				}
			}
		}
	}

	if (!loaded) {
		BMimeType generic("application/octet-stream");
		if (TryGetIcon(generic, outIcon, size))
			loaded = true;
	}

	if (!loaded) {
		delete outIcon;
		// Nessuna icona scalabile: ripiego a 32 px (il disegno la scala).
		if (size != 32)
			return LoadBestIconForRef(ref, signatureOpt, 32);
		return nullptr;
	}
	return outIcon;
}
