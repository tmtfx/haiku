#include "AddOn.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <MediaDefs.h>
#include <MediaNode.h>

#include "DriverInterface.h"
#include "Producer.h"

static bool sEnableMediaAddonLog = true;
static const char* const kMediaAddonLogPath = "/boot/home/bttv_media_addon.log";


static void
addon_log(const char* fmt, ...)
{
	if (!sEnableMediaAddonLog)
		return;

	char buffer[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	FILE* f = fopen(kMediaAddonLogPath, "a");
	if (f != NULL) {
		fputs(buffer, f);
		fflush(f);
		fsync(fileno(f));
		fclose(f);
	}
}


static const char* const kBttvDriverPath = "/dev/video/bttv/0";


BttvMediaAddOn::BttvMediaAddOn(image_id image)
	:
	BMediaAddOn(image)
{
	addon_log("[ADDON] BttvMediaAddOn::BttvMediaAddOn: ENTER\n");

	fFlavorInfo.name = "Bt848/Bt878 Composite Capture";
	fFlavorInfo.info = "Media source for the bttv Bt848/Bt878 composite capture driver";
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

	addon_log("[ADDON] BttvMediaAddOn: probing %s...\n", kBttvDriverPath);
	int driver = open(kBttvDriverPath, O_RDWR);
	if (driver < 0) {
		addon_log("[ADDON] BttvMediaAddOn: open FAILED, errno=%d (%s)\n", errno, strerror(errno));
		fInitStatus = B_ENTRY_NOT_FOUND;
	} else {
		addon_log("[ADDON] BttvMediaAddOn: open SUCCESS, closing probe fd\n");
		close(driver);
	}

	addon_log("[ADDON] BttvMediaAddOn::BttvMediaAddOn: EXIT (status=%s)\n", strerror(fInitStatus));
}


BttvMediaAddOn::~BttvMediaAddOn()
{
	addon_log("[ADDON] BttvMediaAddOn::~BttvMediaAddOn: destroyed\n");
}


status_t
BttvMediaAddOn::InitCheck(const char** outFailureText)
{
	addon_log("[ADDON] BttvMediaAddOn::InitCheck: status=%s\n", strerror(fInitStatus));
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
	int32 count = fInitStatus == B_OK ? 1 : 0;
	addon_log("[ADDON] BttvMediaAddOn::CountFlavors: returning %" B_PRId32 "\n", count);
	return count;
}


status_t
BttvMediaAddOn::GetFlavorAt(int32 index, const flavor_info** outInfo)
{
	addon_log("[ADDON] BttvMediaAddOn::GetFlavorAt: index=%" B_PRId32 "\n", index);
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
	addon_log("[ADDON] BttvMediaAddOn::InstantiateNodeFor: ENTER\n");

	if (outError != NULL)
		*outError = B_ERROR;

	if (fInitStatus != B_OK || info == NULL
		|| info->internal_id != fFlavorInfo.internal_id) {
		addon_log("[ADDON] BttvMediaAddOn::InstantiateNodeFor: invalid flavor or not ready\n");
		return NULL;
	}

	addon_log("[ADDON] BttvMediaAddOn::InstantiateNodeFor: creating BttvProducer...\n");
	BttvProducer* node = new BttvProducer(this, fFlavorInfo.name,
		fFlavorInfo.internal_id, kBttvDriverPath);
	if (node == NULL) {
		addon_log("[ADDON] BttvMediaAddOn::InstantiateNodeFor: out of memory\n");
		return NULL;
	}

	if (node->InitCheck() != B_OK) {
		addon_log("[ADDON] BttvMediaAddOn::InstantiateNodeFor: node->InitCheck FAILED (%s)\n",
			strerror(node->InitCheck()));
		if (outError != NULL)
			*outError = node->InitCheck();
		delete node;
		return NULL;
	}

	if (outError != NULL)
		*outError = B_OK;

	addon_log("[ADDON] BttvMediaAddOn::InstantiateNodeFor: EXIT SUCCESS\n");
	return node;
}


extern "C" _EXPORT BMediaAddOn*
make_media_addon(image_id image)
{
	addon_log("[ADDON] make_media_addon: ENTER\n");
	BMediaAddOn* addon = new BttvMediaAddOn(image);
	addon_log("[ADDON] make_media_addon: EXIT (addon=%p)\n", addon);
	return addon;
}
