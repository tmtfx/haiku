/*
 * VolumeIcons.cpp - vedi VolumeIcons.h. Licenza: MIT.
 */

#include "VolumeIcons.h"

#include <math.h>
#include <string.h>

#include <Bitmap.h>
#include <Directory.h>
#include <DiskDevice.h>
#include <IconUtils.h>
#include <Mime.h>			// get_device_icon(), get_named_icon()
#include <MimeType.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Partition.h>
#include <Path.h>
#include <TypeConstants.h>	// B_VECTOR_ICON_TYPE
#include <Volume.h>

namespace volumeicons {


// Il MIME type con cui Tracker identifica i volumi generici.
static const char* const kGenericVolumeMimeType = "application/x-vnd.Be-volume";


static BBitmap*
_NewBitmap(float px)
{
	int32 dim = (int32)ceilf(px);
	if (dim < 8)
		dim = 8;

	BBitmap* bitmap = new BBitmap(BRect(0, 0, dim - 1, dim - 1), B_RGBA32);
	if (bitmap->InitCheck() != B_OK) {
		delete bitmap;
		return NULL;
	}
	return bitmap;
}


// Le icone raster "storiche" esistono solo a 16 e 32 px e sono in B_CMAP8.
static BBitmap*
_RenderRaster(const uint8* data, size_t size)
{
	int32 dim;
	if (size == 16 * 16)
		dim = 16;
	else if (size == 32 * 32)
		dim = 32;
	else
		return NULL;

	BBitmap source(BRect(0, 0, dim - 1, dim - 1), B_CMAP8);
	if (source.InitCheck() != B_OK)
		return NULL;
	memcpy(source.Bits(), data, size);

	BBitmap* result = _NewBitmap((float)dim);
	if (result == NULL)
		return NULL;
	if (BIconUtils::ConvertFromCMAP8(&source, result) != B_OK) {
		delete result;
		return NULL;
	}
	return result;
}


BBitmap*
RenderVector(const uint8* data, size_t size, float px)
{
	if (data == NULL || size == 0)
		return NULL;

	BBitmap* bitmap = _NewBitmap(px);
	if (bitmap == NULL)
		return NULL;

	if (BIconUtils::GetVectorIcon(data, size, bitmap) != B_OK) {
		delete bitmap;
		return NULL;
	}
	return bitmap;
}


BBitmap*
FromNode(BNode& node, float px)
{
	BNodeInfo info(&node);
	if (info.InitCheck() != B_OK)
		return NULL;

	uint8* data = NULL;
	size_t size = 0;
	type_code type = B_ANY_TYPE;

	// Legge l'attributo BEOS:ICON (HVIF nella stragrande maggioranza dei casi).
	if (info.GetIcon(&data, &size, &type) != B_OK || data == NULL) {
		delete[] data;
		return NULL;
	}

	BBitmap* icon = NULL;
	if (type == B_VECTOR_ICON_TYPE)
		icon = RenderVector(data, size, px);
	else
		icon = _RenderRaster(data, size);

	delete[] data;
	return icon;
}


BBitmap*
FromDevicePath(const char* devicePath, float px)
{
	if (devicePath == NULL || devicePath[0] == '\0')
		return NULL;

	// 1) HVIF: viene rasterizzato a px esatti, quindi resta nitido anche
	//    con righe alte 40/48 px.
	uint8* data = NULL;
	size_t size = 0;
	type_code type = B_ANY_TYPE;
	if (get_device_icon(devicePath, &data, &size, &type) == B_OK
			&& data != NULL) {
		BBitmap* icon = RenderVector(data, size, px);
		delete[] data;
		if (icon != NULL)
			return icon;
	}

	// 2) Ripiego: vecchia icona bitmap 32x32 (la view la scalera').
	//    Nota: non chiederla a una dimensione diversa da 32, perche' il
	//    percorso legacy di get_device_icon() lavora solo a 16/32 px.
	BBitmap* bitmap = _NewBitmap(32);
	if (bitmap == NULL)
		return NULL;

	if (get_device_icon(devicePath, bitmap, B_LARGE_ICON) == B_OK)
		return bitmap;

	delete bitmap;
	return NULL;
}


BBitmap*
GenericVolume(float px)
{
	BMimeType type(kGenericVolumeMimeType);

	uint8* data = NULL;
	size_t size = 0;
	if (type.GetIcon(&data, &size) == B_OK && data != NULL) {
		BBitmap* icon = RenderVector(data, size, px);
		delete[] data;
		if (icon != NULL)
			return icon;
	}

	// Ultimissima spiaggia: icona MIME a 32 px.
	BBitmap* bitmap = _NewBitmap(32);
	if (bitmap == NULL)
		return NULL;
	if (type.GetIcon(bitmap, B_LARGE_ICON) == B_OK)
		return bitmap;

	delete bitmap;
	return NULL;
}


BBitmap*
ForVolume(const BVolume& volume, float px)
{
	if (volume.InitCheck() != B_OK)
		return NULL;

	// a) L'icona scelta dall'utente sta su BEOS:ICON del root del volume
	//    (e' quella che mostra Tracker: BVolume::GetIcon() la ignora).
	BDirectory root;
	if (volume.GetRootDirectory(&root) == B_OK) {
		BBitmap* icon = FromNode(root, px);
		if (icon != NULL)
			return icon;
	}

	// b) Icona del dispositivo (HVIF dal driver, poi eventualmente raster).
	uint8* data = NULL;
	size_t size = 0;
	type_code type = B_ANY_TYPE;
	if (volume.GetIcon(&data, &size, &type) == B_OK && data != NULL) {
		BBitmap* icon = NULL;
		if (type == B_VECTOR_ICON_TYPE)
			icon = RenderVector(data, size, px);
		else
			icon = _RenderRaster(data, size);
		delete[] data;
		if (icon != NULL)
			return icon;
	}

	BBitmap* bitmap = _NewBitmap(32);
	if (bitmap == NULL)
		return NULL;
	if (volume.GetIcon(bitmap, B_LARGE_ICON) == B_OK)
		return bitmap;

	delete bitmap;
	return NULL;
}


BBitmap*
ForPartition(const BPartition* partition, float px)
{
	if (partition == NULL)
		return NULL;

	// Montata: si parte dal volume, cosi' si rispetta l'icona personalizzata.
	if (partition->IsMounted()) {
		BVolume volume;
		if (partition->GetVolume(&volume) == B_OK) {
			BBitmap* icon = ForVolume(volume, px);
			if (icon != NULL)
				return icon;
		}
	}

	// Non montata (o volume senza icona): BPartition::GetIcon() chiede
	// l'icona al driver del disco, che risponde con l'HVIF giusto
	// (drive-harddisk / drive-optical / drive-removable-media-usb / ...).
	uint8* data = NULL;
	size_t size = 0;
	type_code type = B_ANY_TYPE;
	if (partition->GetIcon(&data, &size, &type) == B_OK && data != NULL) {
		BBitmap* icon = NULL;
		if (type == B_VECTOR_ICON_TYPE)
			icon = RenderVector(data, size, px);
		else
			icon = _RenderRaster(data, size);
		delete[] data;
		if (icon != NULL)
			return icon;
	}

	// E poi il vecchio ioctl B_GET_ICON, se il driver lo implementa ancora.
	BBitmap* bitmap = _NewBitmap(32);
	if (bitmap != NULL) {
		if (partition->GetIcon(bitmap, B_LARGE_ICON) == B_OK)
			return bitmap;
		delete bitmap;
	}

	// Se il driver non sa nulla, meglio l'icona generica di Tracker (un bel
	// disco) che il glifo "mount" del nostro .rdef.
	return GenericVolume(px);
}


}	// namespace volumeicons
