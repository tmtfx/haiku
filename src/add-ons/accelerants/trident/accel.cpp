/*
 * Copyright 2026, Gemini CLI. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Gemini CLI <gemini-cli@google.com>
 */


#include "accel.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>


AccelerantInfo gInfo;


static status_t 
InitCommon(int fileDesc)
{
	gInfo.deviceFileDesc = fileDesc;

	TridentGetPrivateData gpd;
	gpd.magic = TRIDENT_PRIVATE_DATA_MAGIC;

	status_t result = ioctl(gInfo.deviceFileDesc, TRIDENT_GET_PRIVATE_DATA, &gpd, sizeof(gpd));
	if (result != B_OK)
		return result;

	gInfo.sharedInfoArea = clone_area("Trident shared info", (void**)&(gInfo.sharedInfo),
		B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, gpd.sharedInfoArea);
	if (gInfo.sharedInfoArea < 0)
		return gInfo.sharedInfoArea;

	gInfo.regsArea = clone_area("Trident regs area", (void**)&(gInfo.regs),
		B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, gInfo.sharedInfo->regsArea);
	if (gInfo.regsArea < 0) {
		delete_area(gInfo.sharedInfoArea);
		return gInfo.regsArea;
	}

	return B_OK;
}


static void 
UninitCommon(void)
{
	delete_area(gInfo.regsArea);
	gInfo.regs = 0;

	delete_area(gInfo.sharedInfoArea);
	gInfo.sharedInfo = 0;
}


status_t 
InitAccelerant(int fileDesc)
{
	TRACE("Enter InitAccelerant()\n");

	gInfo.bAccelerantIsClone = false;

	status_t result = InitCommon(fileDesc);
	if (result == B_OK) {
		SharedInfo& si = *gInfo.sharedInfo;

		TRACE("Vendor ID: 0x%X, Device ID: 0x%X\n", si.vendorID, si.deviceID);

		si.maxFrameBufferSize = si.videoMemSize;
		si.cursorOffset = si.maxFrameBufferSize - 4096;

		if (si.bAccelerantInUse) {
			result = B_NOT_ALLOWED;
		} else {
			result = si.engineLock.Init("Trident engine lock");
			if (result == B_OK) {
				si.bAccelerantInUse = true;
			}
		}

		if (result != B_OK)
			UninitCommon();
	}

	TRACE("Leave InitAccelerant(), result: 0x%X\n", result);
	return result;
}


ssize_t 
AccelerantCloneInfoSize(void)
{
	return B_OS_NAME_LENGTH;
}


void 
GetAccelerantCloneInfo(void* data)
{
	ioctl(gInfo.deviceFileDesc, TRIDENT_DEVICE_NAME, data, B_OS_NAME_LENGTH);
}


status_t 
CloneAccelerant(void* data)
{
	TRACE("Enter CloneAccelerant()\n");

	char path[MAXPATHLEN] = "/dev/";
	strcat(path, (const char*)data);

	gInfo.deviceFileDesc = open(path, B_READ_WRITE);
	if (gInfo.deviceFileDesc < 0)
		return errno;

	gInfo.bAccelerantIsClone = true;

	status_t result = InitCommon(gInfo.deviceFileDesc);
	if (result != B_OK) {
		close(gInfo.deviceFileDesc);
		return result;
	}

	result = gInfo.modeListArea = clone_area("Trident cloned display_modes",
		(void**) &gInfo.modeList, B_ANY_ADDRESS, B_READ_AREA,
		gInfo.sharedInfo->modeArea);
	if (result < 0) {
		UninitCommon();
		close(gInfo.deviceFileDesc);
		return result;
	}

	TRACE("Leave CloneAccelerant()\n");
	return B_OK;
}


void 
UninitAccelerant(void)
{
	delete_area(gInfo.modeListArea);
	gInfo.modeList = NULL;

	UninitCommon();

	if (gInfo.bAccelerantIsClone)
		close(gInfo.deviceFileDesc);
}


sem_id 
AccelerantRetraceSemaphore(void)
{
	return B_ERROR;
}


status_t 
GetAccelerantDeviceInfo(accelerant_device_info* adi)
{
	SharedInfo& si = *gInfo.sharedInfo;

	adi->version = 1;
	strcpy(adi->name, "Trident Chipset");
	strcpy(adi->chipset, si.chipName);
	strcpy(adi->serial_no, "unknown");
	adi->memory = si.maxFrameBufferSize;
	adi->dac_speed = 230;

	return B_OK;
}
