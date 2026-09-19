#include "AddOn.h"

#include <fcntl.h>
#include <unistd.h>

#include <MediaDefs.h>
#include <MediaNode.h>

#include "DriverInterface.h"
#include "Producer.h"


static const char* const kBttvDriverPath = "/dev/video/bttv/0";


BttvMediaAddOn::BttvMediaAddOn(image_id image)
	:
	BMediaAddOn(image)
{
	fFlavorInfo.name = "Bt878 Composite Capture";
	fFlavorInfo.info = "Media source for the bttv Bt878 composite capture driver";
	fFlavorInfo.kinds = B_BUFFER_PRODUCER | B_CONTROLLABLE | B_PHYSICAL_INPUT;
	fFlavorInfo.flavor_flags = 0;
	fFlavorInfo.internal_id = 0;
	fFlavorInfo.possible_count = 1;
	fFlavorInfo.in_format_count = 0;
	fFlavorInfo.in_format_flags = 0;
	fFlavorInfo.in_formats = NULL;
	fFlavorInfo.out_format_count = 1;
	fFlavorInfo.out_format_flags = 0;

	fMediaFormat.type = B_MEDIA_RAW_VIDEO;
	fMediaFormat.u.raw_video = media_raw_video_format::wildcard;
	fMediaFormat.u.raw_video.interlace = 1;
	fMediaFormat.u.raw_video.display.format = B_YCbCr422;
	fFlavorInfo.out_formats = &fMediaFormat;

	fInitStatus = B_OK;

	int driver = open(kBttvDriverPath, O_RDWR);
	if (driver < 0)
		fInitStatus = B_ENTRY_NOT_FOUND;
	else
		close(driver);
}


BttvMediaAddOn::~BttvMediaAddOn()
{
}


status_t
BttvMediaAddOn::InitCheck(const char** outFailureText)
{
	if (fInitStatus != B_OK) {
		if (outFailureText != NULL)
			*outFailureText = "Unable to open /dev/video/bttv/0";
		return fInitStatus;
	}

	return B_OK;
}


int32
BttvMediaAddOn::CountFlavors()
{
	return fInitStatus == B_OK ? 1 : 0;
}


status_t
BttvMediaAddOn::GetFlavorAt(int32 index, const flavor_info** outInfo)
{
	if (fInitStatus != B_OK)
		return fInitStatus;
	if (index != 0)
		return B_BAD_INDEX;

	*outInfo = &fFlavorInfo;
	return B_OK;
}


BMediaNode*
BttvMediaAddOn::InstantiateNodeFor(const flavor_info* info, BMessage* config,
	status_t* outError)
{
	(void)config;

	if (outError != NULL)
		*outError = B_ERROR;

	if (fInitStatus != B_OK || info == NULL
		|| info->internal_id != fFlavorInfo.internal_id) {
		return NULL;
	}

	BttvProducer* node = new BttvProducer(this, fFlavorInfo.name,
		fFlavorInfo.internal_id, kBttvDriverPath);
	if (node == NULL)
		return NULL;

	if (node->InitCheck() != B_OK) {
		if (outError != NULL)
			*outError = node->InitCheck();
		delete node;
		return NULL;
	}

	if (outError != NULL)
		*outError = B_OK;
	return node;
}


extern "C" _EXPORT BMediaAddOn*
make_media_addon(image_id image)
{
	return new BttvMediaAddOn(image);
}
