/*
 * Copyright 2005-2009, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2016, Jessica Hamilton, jessica.l.hamilton@gmail.com.
 * Distributed under the terms of the MIT License.
 */


#include <OS.h>
#include <KernelExport.h>
#include <SupportDefs.h>
#include <PCI.h>
#include <frame_buffer_console.h>
#include <boot_item.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>

#include "DriverInterface.h"
#include "driver.h"
#include "device.h"


#define TRACE_DRIVER
#ifdef TRACE_DRIVER
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif

#define MAX_CARDS 1


int32 api_version = B_CUR_DRIVER_API_VERSION;

char* gDeviceNames[MAX_CARDS + 1];
vesa_info* gDeviceInfo[MAX_CARDS];
mutex gLock;

static bool
supports_device(uint16_t vendorID, uint16_t deviceID)
{
	return vendorID == 0x10de && deviceID == 0x1287;
}


static bool
is_boot_nvidia_gt7xx_device(void)
{
	frame_buffer_boot_info* bufferInfo
		= (frame_buffer_boot_info*)get_boot_item(FRAME_BUFFER_BOOT_INFO, NULL);
	if (bufferInfo == NULL)
		return false;

	pci_module_info* pci;
	if (get_module(B_PCI_MODULE_NAME, (module_info**)&pci) != B_OK)
		return false;

	bool supported = false;
	pci_info info;
	for (int32 index = 0; !supported
			&& pci->get_nth_pci_info(index, &info) == B_OK; index++) {
		if (info.class_base != PCI_display)
			continue;

		for (uint32 i = 0; i < 6; i++) {
			addr_t base = info.u.h0.base_registers[i];
			size_t size = info.u.h0.base_register_sizes[i];
			if (base <= bufferInfo->physical_frame_buffer
				&& base + size > bufferInfo->physical_frame_buffer) {
				supported = supports_device(info.vendor_id, info.device_id);
				break;
			}
		}
	}

	put_module(B_PCI_MODULE_NAME);
	return supported;
}


extern "C" const char**
publish_devices(void)
{
	TRACE((DEVICE_NAME ": publish_devices()\n"));
	return (const char**)gDeviceNames;
}


extern "C" status_t
init_hardware(void)
{
	TRACE((DEVICE_NAME ": init_hardware()\n"));

	return is_boot_nvidia_gt7xx_device() ? B_OK : B_ERROR;
}


extern "C" status_t
init_driver(void)
{
	TRACE((DEVICE_NAME ": init_driver()\n"));

	if (!is_boot_nvidia_gt7xx_device())
		return B_ERROR;

	gDeviceInfo[0] = (vesa_info*)malloc(sizeof(vesa_info));
	if (gDeviceInfo[0] == NULL)
		return B_NO_MEMORY;

	memset(gDeviceInfo[0], 0, sizeof(vesa_info));

	gDeviceNames[0] = strdup("graphics/nvidia_gt7xx");
	if (gDeviceNames[0] == NULL) {
		free(gDeviceInfo[0]);
		return B_NO_MEMORY;
	}

	gDeviceNames[1] = NULL;

	mutex_init(&gLock, "nvidia_gt7xx lock");
	return B_OK;
}


extern "C" void
uninit_driver(void)
{
	TRACE((DEVICE_NAME ": uninit_driver()\n"));
	mutex_destroy(&gLock);

	// free device related structures
	char* name;
	for (int32 index = 0; (name = gDeviceNames[index]) != NULL; index++) {
		free(gDeviceInfo[index]);
		free(name);
	}
}


extern "C" device_hooks*
find_device(const char* name)
{
	int index;

	TRACE((DEVICE_NAME ": find_device()\n"));

	for (index = 0; gDeviceNames[index] != NULL; index++) {
		if (!strcmp(name, gDeviceNames[index]))
			return &gDeviceHooks;
	}

	return NULL;
}
