/*
 * Copyright 2026, GitHub Copilot. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "accelerant.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#define KYRO_ACCEL_TRACE(x...) debug_printf("kyro.accelerant: " x)


AccelerantInfo gInfo = {
	-1,
	false,
	-1,
	NULL,
	-1,
	NULL,
	-1,
	NULL,
	-1,
	NULL
};


static void
disable_hardware_cursor(void)
{
	static const uint32 kDACCursorAddr = 0x1408;
	static const uint32 kDACCursorCtrl = 0x140c;

	if (gInfo.regs == NULL)
		return;

	*(volatile uint32*)(gInfo.regs + kDACCursorCtrl) = 0;
	*(volatile uint32*)(gInfo.regs + kDACCursorAddr) = 0;
	KYRO_ACCEL_TRACE("hardware cursor disabled\n");
}


static status_t
init_common(int fileDesc, bool isClone)
{
	KYRO_ACCEL_TRACE("init_common begin fd=%d isClone=%d\n", fileDesc, isClone);
	gInfo.deviceFile = fileDesc;
	gInfo.isClone = isClone;
	gInfo.modeListArea = -1;
	gInfo.modeList = NULL;
	gInfo.regsArea = -1;
	gInfo.frameBufferArea = -1;

	KyroGetPrivateData data;
	data.magic = KYRO_PRIVATE_DATA_MAGIC;
	status_t status = ioctl(gInfo.deviceFile, KYRO_GET_PRIVATE_DATA, &data,
		sizeof(data));
	if (status != B_OK) {
		KYRO_ACCEL_TRACE("init_common: KYRO_GET_PRIVATE_DATA failed 0x%" B_PRIx32 "\n",
			status);
		return status;
	}

	gInfo.sharedInfoArea = clone_area("kyro shared info",
		(void**)&gInfo.sharedInfo, B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA,
		data.sharedInfoArea);
	if (gInfo.sharedInfoArea < B_OK) {
		KYRO_ACCEL_TRACE("init_common: clone shared info failed 0x%" B_PRIx32 "\n",
			gInfo.sharedInfoArea);
		return gInfo.sharedInfoArea;
	}

	gInfo.regsArea = clone_area("kyro regs", (void**)&gInfo.regs, B_ANY_ADDRESS,
		B_READ_AREA | B_WRITE_AREA, gInfo.sharedInfo->regsArea);
	if (gInfo.regsArea < B_OK) {
		KYRO_ACCEL_TRACE("init_common: clone regs failed 0x%" B_PRIx32 "\n",
			gInfo.regsArea);
		delete_area(gInfo.sharedInfoArea);
		gInfo.sharedInfoArea = -1;
		gInfo.sharedInfo = NULL;
		return gInfo.regsArea;
	}

	gInfo.frameBufferArea = clone_area("kyro framebuffer",
		(void**)&gInfo.frameBuffer, B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA,
		gInfo.sharedInfo->frameBufferArea);
	if (gInfo.frameBufferArea < B_OK) {
		KYRO_ACCEL_TRACE("init_common: clone framebuffer failed 0x%" B_PRIx32 "\n",
			gInfo.frameBufferArea);
		delete_area(gInfo.regsArea);
		delete_area(gInfo.sharedInfoArea);
		gInfo.regsArea = -1;
		gInfo.sharedInfoArea = -1;
		gInfo.sharedInfo = NULL;
		gInfo.regs = NULL;
		return gInfo.frameBufferArea;
	}

	KYRO_ACCEL_TRACE("init_common done sharedArea=%" B_PRId32 " regsArea=%" B_PRId32
		" fbArea=%" B_PRId32 "\n", gInfo.sharedInfoArea, gInfo.regsArea,
		gInfo.frameBufferArea);
	return B_OK;
}


static void
uninit_common(void)
{
	KYRO_ACCEL_TRACE("uninit_common begin isClone=%d\n", gInfo.isClone);
	if (gInfo.frameBufferArea >= B_OK)
		delete_area(gInfo.frameBufferArea);
	if (gInfo.regsArea >= B_OK)
		delete_area(gInfo.regsArea);
	if (gInfo.sharedInfoArea >= B_OK)
		delete_area(gInfo.sharedInfoArea);

	if (gInfo.isClone && gInfo.deviceFile >= 0)
		close(gInfo.deviceFile);

	gInfo.deviceFile = -1;
	gInfo.sharedInfoArea = -1;
	gInfo.sharedInfo = NULL;
	gInfo.regsArea = -1;
	gInfo.regs = NULL;
	gInfo.frameBufferArea = -1;
	gInfo.frameBuffer = NULL;
	KYRO_ACCEL_TRACE("uninit_common done\n");
}


status_t
InitAccelerant(int fileDesc)
{
	KYRO_ACCEL_TRACE("InitAccelerant begin fd=%d\n", fileDesc);
	status_t status = init_common(fileDesc, false);
	if (status != B_OK)
		return status;

	if (gInfo.sharedInfo->accelerantInUse) {
		uninit_common();
		return B_NOT_ALLOWED;
	}

	status = CreateModeList();
	if (status != B_OK) {
		KYRO_ACCEL_TRACE("InitAccelerant: CreateModeList failed 0x%" B_PRIx32 "\n",
			status);
		uninit_common();
		return status;
	}

	disable_hardware_cursor();
	gInfo.sharedInfo->accelerantInUse = true;
	KYRO_ACCEL_TRACE("InitAccelerant done modeCount=%" B_PRIu32 "\n",
		gInfo.sharedInfo->modeCount);
	return B_OK;
}


ssize_t
AccelerantCloneInfoSize(void)
{
	return B_PATH_NAME_LENGTH;
}


void
GetAccelerantCloneInfo(void* data)
{
	KYRO_ACCEL_TRACE("GetAccelerantCloneInfo\n");
	ioctl(gInfo.deviceFile, KYRO_DEVICE_NAME, data, B_PATH_NAME_LENGTH);
}


status_t
CloneAccelerant(void* data)
{
	KYRO_ACCEL_TRACE("CloneAccelerant begin\n");
	char path[MAXPATHLEN];
	strlcpy(path, "/dev/", sizeof(path));
	strlcat(path, (const char*)data, sizeof(path));

	int fd = open(path, B_READ_WRITE);
	if (fd < 0)
		return errno;

	status_t status = init_common(fd, true);
	if (status != B_OK) {
		KYRO_ACCEL_TRACE("CloneAccelerant: init_common failed 0x%" B_PRIx32 "\n",
			status);
		close(fd);
		return status;
	}

	gInfo.modeListArea = clone_area("kyro mode list", (void**)&gInfo.modeList,
		B_ANY_ADDRESS, B_READ_AREA, gInfo.sharedInfo->modeArea);
	if (gInfo.modeListArea < B_OK) {
		status = gInfo.modeListArea;
		KYRO_ACCEL_TRACE("CloneAccelerant: clone mode list failed 0x%" B_PRIx32 "\n",
			status);
		uninit_common();
		return status;
	}

	disable_hardware_cursor();
	KYRO_ACCEL_TRACE("CloneAccelerant done\n");
	return B_OK;
}


void
UninitAccelerant(void)
{
	KYRO_ACCEL_TRACE("UninitAccelerant begin\n");
	if (!gInfo.isClone && gInfo.sharedInfo != NULL)
		gInfo.sharedInfo->accelerantInUse = false;

	if (gInfo.modeListArea >= B_OK)
		delete_area(gInfo.modeListArea);
	gInfo.modeListArea = -1;
	gInfo.modeList = NULL;

	uninit_common();
	KYRO_ACCEL_TRACE("UninitAccelerant done\n");
}


status_t
GetAccelerantDeviceInfo(accelerant_device_info* info)
{
	info->version = B_ACCELERANT_VERSION;
	strlcpy(info->name, "PowerVR Kyro Driver", sizeof(info->name));
	strlcpy(info->chipset, gInfo.sharedInfo->chipName, sizeof(info->chipset));
	strlcpy(info->serial_no, "unknown", sizeof(info->serial_no));
	info->memory = gInfo.sharedInfo->frameBufferSize;
	info->dac_speed = gInfo.sharedInfo->maxPixelClock / 1000;
	KYRO_ACCEL_TRACE("GetAccelerantDeviceInfo memory=%" B_PRIu32 " dac=%" B_PRIu32 "\n",
		info->memory, info->dac_speed);
	return B_OK;
}


sem_id
AccelerantRetraceSemaphore(void)
{
	return -1;
}
