/*
 * Copyright 2005-2009, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include "device.h"

#include <stdlib.h>
#include <string.h>

#include <Drivers.h>
#include <graphic_driver.h>
#include <image.h>
#include <KernelExport.h>
#include <OS.h>
#include <PCI.h>
#include <SupportDefs.h>
#include <vm/vm.h>

#include "driver.h"
#include "DriverInterface.h"
#include "vesa_private.h"


//#define TRACE_DEVICE
#ifdef TRACE_DEVICE
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


static status_t
device_open(const char* name, uint32 flags, void** _cookie)
{
	int id;

	// find accessed device
	char* thisName;

	// search for device name
	for (id = 0; (thisName = gDeviceNames[id]) != NULL; id++) {
		if (!strcmp(name, thisName))
			break;
	}
	if (thisName == NULL)
		return B_BAD_VALUE;

	vesa_info* info = gDeviceInfo[id];
	dprintf("nvidia_gt7xx: device_open name=%s id=%d open_count=%" B_PRIu32 "\n",
		name, id, info->open_count);

	mutex_lock(&gLock);

	status_t status = B_OK;

	if (info->open_count == 0) {
		// this device has been opened for the first time, so
		// we allocate needed resources and initialize the structure
		if (status == B_OK)
			status = nvidia_gt7xx_init(*info);
		if (status == B_OK)
			info->id = id;
		else
			dprintf("nvidia_gt7xx: device_open vesa_init failed status=%" B_PRId32 "\n",
				status);
	}

	if (status == B_OK) {
		info->open_count++;
		*_cookie = info;
		dprintf("nvidia_gt7xx: device_open success id=%d new_open_count=%" B_PRIu32 "\n",
			id, info->open_count);
	}

	mutex_unlock(&gLock);
	return status;
}


static status_t
device_close(void* cookie)
{
	return B_OK;
}


static status_t
device_free(void* cookie)
{
	struct vesa_info* info = (vesa_info*)cookie;

	mutex_lock(&gLock);

	if (info->open_count-- == 1) {
		// release info structure
		nvidia_gt7xx_uninit(*info);
	}

	mutex_unlock(&gLock);
	return B_OK;
}


static status_t
device_ioctl(void* cookie, uint32 msg, void* buffer, size_t bufferLength)
{
	struct vesa_info* info = (vesa_info*)cookie;

	switch (msg) {
		case B_GET_ACCELERANT_SIGNATURE:
			dprintf(DEVICE_NAME ": acc: %s\n", VESA_ACCELERANT_NAME);
			if (user_strlcpy((char*)buffer, VESA_ACCELERANT_NAME,
					B_FILE_NAME_LENGTH) < B_OK)
				return B_BAD_ADDRESS;

			return B_OK;

		case VESA_CLONE_FRAME_BUFFER:
		{
			void* dummy;
			area_id area = vm_clone_area(B_CURRENT_TEAM, "cloned framebuffer",
				&dummy, B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, 0,
				info->frame_buffer_area, true);
			if (area < 0)
				return area;

			return _user_get_area_info(area, (area_info*)buffer);
		}

		// needed to share data between kernel and accelerant
		case VESA_GET_PRIVATE_DATA:
			return user_memcpy(buffer, &info->shared_area, sizeof(area_id));

		// needed for cloning
		case VESA_GET_DEVICE_NAME:
			if (user_strlcpy((char*)buffer, gDeviceNames[info->id],
					B_PATH_NAME_LENGTH) < B_OK)
				return B_BAD_ADDRESS;

			return B_OK;

		case VESA_SET_DISPLAY_MODE:
		{
			if (bufferLength != sizeof(display_mode))
				return B_BAD_VALUE;

			display_mode mode;
			if (user_memcpy(&mode, buffer, sizeof(display_mode)) != B_OK)
				return B_BAD_ADDRESS;

			return nvidia_gt7xx_set_display_mode(*info, mode);
		}

		case VESA_GET_DPMS_MODE:
		{
			if (bufferLength != sizeof(uint32))
				return B_BAD_VALUE;

			uint32 mode;
			status_t status = nvidia_gt7xx_get_dpms_mode(*info, mode);
			if (status != B_OK)
				return status;

			return user_memcpy(buffer, &mode, sizeof(mode));
		}

		case VESA_SET_DPMS_MODE:
		{
			if (bufferLength != sizeof(uint32))
				return B_BAD_VALUE;

			uint32 mode;
			if (user_memcpy(&mode, buffer, sizeof(uint32)) != B_OK)
				return B_BAD_ADDRESS;

			return nvidia_gt7xx_set_dpms_mode(*info, mode);
		}

		case NVIDIA_GT7XX_SUBMIT_EVO:
		{
			if (bufferLength != sizeof(nvidia_gt7xx_evo_push))
				return B_BAD_VALUE;

			nvidia_gt7xx_evo_push push;
			if (user_memcpy(&push, buffer, sizeof(push)) != B_OK)
				return B_BAD_ADDRESS;

			return nvidia_gt7xx_submit_evo(*info, push);
		}

		default:
			TRACE((DEVICE_NAME ": ioctl() unknown message %ld (length = %lu)\n",
				msg, bufferLength));
			break;
	}

	return B_DEV_INVALID_IOCTL;
}


static status_t
device_read(void* /*cookie*/, off_t /*pos*/, void* /*buffer*/, size_t* _length)
{
	*_length = 0;
	return B_NOT_ALLOWED;
}


static status_t
device_write(void* /*cookie*/, off_t /*pos*/, const void* /*buffer*/,
	size_t* _length)
{
	*_length = 0;
	return B_NOT_ALLOWED;
}


device_hooks gDeviceHooks = {
	device_open,
	device_close,
	device_free,
	device_ioctl,
	device_read,
	device_write,
	NULL,
	NULL,
	NULL,
	NULL
};
