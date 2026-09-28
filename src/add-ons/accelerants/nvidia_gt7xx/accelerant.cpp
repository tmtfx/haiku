/*
 * Copyright 2005-2008, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "accelerant_protos.h"
#include "accelerant.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <syslog.h>

#include <AutoDeleterOS.h>


struct accelerant_info *gInfo;


//	#pragma mark -


/*!	This is the common accelerant_info initializer. It is called by
	both, the first accelerant and all clones.
*/
static status_t
init_common(int device, bool isClone)
{
	debug_printf("nvidia_gt7xx.accelerant: init_common(device=%d, isClone=%d)\n",
		device, isClone);

	gInfo = (accelerant_info *)malloc(sizeof(accelerant_info));
	MemoryDeleter infoDeleter(gInfo);
	if (gInfo == NULL) {
		debug_printf("nvidia_gt7xx.accelerant: failed to allocate accelerant_info\n");
		return B_NO_MEMORY;
	}

	memset(gInfo, 0, sizeof(accelerant_info));

	gInfo->is_clone = isClone;
	gInfo->device = device;
	gInfo->frame_buffer_area = -1;
	gInfo->regs_area = -1;

	// get basic info from driver
	area_id sharedArea;
	if (ioctl(device, VESA_GET_PRIVATE_DATA, &sharedArea, sizeof(area_id)) != 0) {
		debug_printf("nvidia_gt7xx.accelerant: ioctl(VESA_GET_PRIVATE_DATA) failed: %s\n",
			strerror(errno));
		return B_ERROR;
	}

	debug_printf("nvidia_gt7xx.accelerant: sharedArea=%d, cloning...\n", (int)sharedArea);

	AreaDeleter sharedDeleter(clone_area("nvidia_gt7xx shared info",
		(void **)&gInfo->shared_info, B_ANY_ADDRESS,
		B_READ_AREA | B_WRITE_AREA, sharedArea));
	status_t status = gInfo->shared_info_area = sharedDeleter.Get();
	if (status < B_OK) {
		debug_printf("nvidia_gt7xx.accelerant: clone_area shared info failed: %" B_PRId32 "\n",
			status);
		return status;
	}

	debug_printf("nvidia_gt7xx.accelerant: cloned shared info at %p (vendor=0x%04x, dev=0x%04x, name=%s)\n",
		gInfo->shared_info, gInfo->shared_info->vendor_id, gInfo->shared_info->device_id,
		gInfo->shared_info->name);

	if (gInfo->shared_info->registers_area >= B_OK) {
		debug_printf("nvidia_gt7xx.accelerant: registers_area=%d (size=0x%08" B_PRIx32 "), cloning...\n",
			(int)gInfo->shared_info->registers_area, gInfo->shared_info->registers_size);

		AreaDeleter regsDeleter(clone_area("nvidia_gt7xx regs",
			(void**)&gInfo->registers, B_ANY_ADDRESS,
			B_READ_AREA | B_WRITE_AREA, gInfo->shared_info->registers_area));
		status = gInfo->regs_area = regsDeleter.Get();
		if (status < B_OK) {
			debug_printf("nvidia_gt7xx.accelerant: clone_area regs failed: %" B_PRId32 "\n", status);
			return status;
		}
		debug_printf("nvidia_gt7xx.accelerant: cloned registers at %p\n", gInfo->registers);
		regsDeleter.Detach();
	}

	infoDeleter.Detach();
	sharedDeleter.Detach();
	debug_printf("nvidia_gt7xx.accelerant: init_common succeeded!\n");
	return B_OK;
}


/*!	Cleans up everything done by a successful init_common(). */
static void
uninit_common(void)
{
	debug_printf("nvidia_gt7xx.accelerant: uninit_common()\n");

	if (gInfo->regs_area >= B_OK)
		delete_area(gInfo->regs_area);
	gInfo->regs_area = -1;
	gInfo->registers = NULL;
	if (gInfo->shared_info_area >= B_OK)
		delete_area(gInfo->shared_info_area);
	gInfo->shared_info_area = -1;
	gInfo->shared_info = NULL;

	if (gInfo->is_clone)
		close(gInfo->device);

	free(gInfo);
}


//	#pragma mark - public accelerant functions


/*!	Init primary accelerant */
status_t
nvidia_gt7xx_init_accelerant(int device)
{
	debug_printf("nvidia_gt7xx.accelerant: init_accelerant(device=%d) START\n", device);

	status_t status = init_common(device, false);
	if (status != B_OK) {
		debug_printf("nvidia_gt7xx.accelerant: init_common failed: %" B_PRId32 "\n", status);
		return status;
	}

	status = create_mode_list();
	if (status != B_OK) {
		debug_printf("nvidia_gt7xx.accelerant: create_mode_list failed: %" B_PRId32 "\n", status);
		uninit_common();
		return status;
	}

	debug_printf("nvidia_gt7xx.accelerant: create_mode_list succeeded, mode_count=%" B_PRIu32 "\n",
		gInfo->shared_info->mode_count);

	// Initialize current mode completely from the mode list
	nvidia_gt7xx_propose_display_mode(&gInfo->shared_info->current_mode, NULL, NULL);

	debug_printf("nvidia_gt7xx.accelerant: init_accelerant COMPLETE! Current mode: %ux%u space=0x%08"
		B_PRIx32 " clock=%" B_PRIu32 " kHz\n",
		gInfo->shared_info->current_mode.virtual_width,
		gInfo->shared_info->current_mode.virtual_height,
		gInfo->shared_info->current_mode.space,
		gInfo->shared_info->current_mode.timing.pixel_clock);

	return B_OK;
}


ssize_t
nvidia_gt7xx_accelerant_clone_info_size(void)
{
	return B_PATH_NAME_LENGTH;
}


void
nvidia_gt7xx_get_accelerant_clone_info(void *info)
{
	ioctl(gInfo->device, VESA_GET_DEVICE_NAME, info, B_PATH_NAME_LENGTH);
}


status_t
nvidia_gt7xx_clone_accelerant(void *info)
{
	debug_printf("nvidia_gt7xx.accelerant: clone_accelerant(%s)\n", (const char*)info);

	char path[MAXPATHLEN];
	strcpy(path, "/dev/");
	strcat(path, (const char *)info);

	int fd = open(path, B_READ_WRITE);
	if (fd < 0) {
		debug_printf("nvidia_gt7xx.accelerant: open(%s) failed: %s\n", path, strerror(errno));
		return errno;
	}

	status_t status = init_common(fd, true);
	if (status != B_OK)
		goto err1;

	status = gInfo->mode_list_area = clone_area(
		"nvidia_gt7xx cloned modes", (void **)&gInfo->mode_list,
		B_ANY_ADDRESS, B_READ_AREA, gInfo->shared_info->mode_list_area);
	if (status < B_OK) {
		debug_printf("nvidia_gt7xx.accelerant: clone_area modes failed: %" B_PRId32 "\n", status);
		goto err2;
	}

	debug_printf("nvidia_gt7xx.accelerant: clone_accelerant SUCCESS\n");
	return B_OK;

err2:
	uninit_common();
err1:
	close(fd);
	return status;
}


void
nvidia_gt7xx_uninit_accelerant(void)
{
	debug_printf("nvidia_gt7xx.accelerant: uninit_accelerant()\n");

	delete_area(gInfo->mode_list_area);
	gInfo->mode_list = NULL;

	uninit_common();
}


status_t
nvidia_gt7xx_get_accelerant_device_info(accelerant_device_info *info)
{
	info->version = B_ACCELERANT_VERSION;

	strcpy(info->name, "NVIDIA GT7xx native driver");
	if (gInfo->shared_info->name[0] != '\0') {
		strlcpy(info->chipset, gInfo->shared_info->name, 32);
	} else {
		strcpy(info->chipset, "NVIDIA GT7xx");
	}
	strcpy(info->serial_no, "None");

	info->memory = gInfo->shared_info->vram_size;
	return B_OK;
}


sem_id
nvidia_gt7xx_accelerant_retrace_semaphore()
{
	return -1;
}
