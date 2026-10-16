/*
 * Copyright (C) 2001-2004 by Thomas Winischhofer, Vienna, Austria
 * Copyright (C) 1998-2000 by Alan Hourihane, Wigan, England
 * Copyright (C) 2000 Mike Chapman <mike@paranoia.com>
 * Copyright (C) 2000 Juanjo Santamarta <santamarta@ctv.es>
 * Copyright (C) 2000 Mitani Hiroshi <hmitani@drl.mei.co.jp>
 * Copyright (C) 2000 David Thomas <davtom@dream.org.uk>
 * Copyright (C) 2003-2004 Sung-Ching Lin, Ademar Reis
 * Copyright (C) 2003-2004 Eric Anholt <anholt@FreeBSD.org>
 * Copyright (C) 2004-2009 Nan-Hsing Chang <nan_chang@xgitech.com>
 * Copyright (C) 2004-2009 Jong Lin (XGI Technology Inc.)
 * Copyright (C) 2004-2009 Can-Ru Yeou (XGI Technology Inc.)
 * Copyright (C) 2004-2009 Hsuan, Vicent, kuku (XGI Technology Inc.)
 * Copyright (C) 2000 Mark Vojkovich
 * Copyright (C) 2000 Jonathan Bian <jonathan.bian@intel.com>
 * Copyright (C) 2005-2024 Adam Jackson <ajax@redhat.com>
 * Copyright (C) 2005-2024 Alan Barrett <apb@NetBSD.org>
 * Copyright (C) 2005-2024 Alan Coopersmith <alan.coopersmith@oracle.com>
 * Copyright (C) 2005-2024 Alberto Bursi <starshipeleven@outlook.com>
 * Copyright (C) 2005-2024 Chris Ball <cjb@laptop.org>
 * Copyright (C) 2005-2024 Connor Behan <connor.behan@gmail.com>
 * Copyright (C) 2005-2024 Dave Airlie <airlied@redhat.com>
 * Copyright (C) 2005-2024 Egbert Eich <eich@freedesktop.org>
 * Copyright (C) 2005-2024 Emil Velikov <emil.velikov@collabora.com>
 * Copyright (C) 2005-2024 Enrico Weigelt <info@metux.net>
 * Copyright (C) 2005-2024 Fernando Carrijo <fcarrijo@yahoo.com.br>
 * Copyright (C) 2005-2024 Gaetan Nadon <memsize@videotron.ca>
 * Copyright (C) 2005-2024 Ian Romanick <idr@us.ibm.com>
 * Copyright (C) 2005-2024 Jakub Bogusz <qboosh@pld-linux.org>
 * Copyright (C) 2005-2024 James Cloos <cloos@jhcloos.com>
 * Copyright (C) 2005-2024 Jeremy Huddleston <jeremyhu@apple.com>
 * Copyright (C) 2005-2024 Jesse Adkins <jesserayadkins@gmail.com>
 * Copyright (C) 2005-2024 Johannes Obermayr <johannesobermayr@gmx.de>
 * Copyright (C) 2005-2024 Peter Hutterer <peter.hutterer@who-t.net>
 * Copyright (C) 2005-2024 Rémi Cardona <remi@gentoo.org>
 * Copyright (C) 2005-2024 Stefan Dirsch <sndirsch@novell.com>
 * Copyright (C) 2005-2024 Thomas Klausner <wiz@NetBSD.org>
 * Copyright (C) 2005-2024 Trevor Woerner <twoerner@gmail.com>
 * Copyright (C) 2005-2024 Tristan Plumb <freedesktop@trstn.net>
 * Copyright (C) 2004-2009 eXtreme Graphics Innovation Corp, Inc.
 * Copyright (C) 1999-2004 The XFree86 Project, Inc.
 * Copyright (C) 2000-2003 ATI Technologies Inc.
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
 * OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <boot_item.h>
#include <frame_buffer_console.h>
#include <Drivers.h>
#include <KernelExport.h>
#include <PCI.h>
#include <driver_settings.h>
#include <graphic_driver.h>
#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <vm/vm.h>
#include <vesa_info.h>

#include "DriverInterface.h"

#ifdef IS_PIRATI_BUILD
#include "xgi_logo2.h"
#endif

#define MAX_DEVICES 8
#define XGI_TRACE(x...) dprintf("xgi.driver: " x)

int32 api_version = B_CUR_DRIVER_API_VERSION;

struct DeviceInfo {
	uint32		openCount;
	area_id		sharedArea;
	SharedInfo*	sharedInfo;
	vuint8*		regs;
	pci_info	pciInfo;
	char		name[B_PATH_NAME_LENGTH];
};

static DeviceInfo		gDeviceInfo[MAX_DEVICES];
static const char*		gDeviceNames[MAX_DEVICES + 1];
static pci_module_info*	gPCI;

static xgi_settings gSettings = {
	XGI_ACCELERANT_NAME,
	true,
	false
};

static struct {
	uint16 deviceID;
	uint32 chipType;
	const char* name;
} kSupportedChips[] = {
	{ PCI_CHIP_XGIXG20, XGI_XG20, "XGI Volari Z7/Z9" },
	{ PCI_CHIP_XGIXG21, XGI_XG21, "XGI Volari Z9s/Z9m" },
	{ PCI_CHIP_XGIXG27, XGI_XG27, "XGI Volari Z11/Z11M" },
	{ PCI_CHIP_XGIXG40, XGI_XG40, "XGI Volari V3XT/V5/V8" },
	{ PCI_CHIP_XGIXG41, XGI_XG41, "XGI Volari V3/V5" },
	{ PCI_CHIP_XGIXG42, XGI_XG42, "XGI Volari XG42" },
	{ PCI_CHIP_XGIXG47, XGI_XG47, "XGI Volari 8300 (XP10)" },
	{ 0, 0, NULL }
};

static status_t device_open(const char* name, uint32 flags, void** cookie);
static status_t device_close(void* cookie);
static status_t device_free(void* cookie);
static status_t device_ioctl(void* cookie, uint32 msg, void* buf, size_t len);
static status_t device_read(void* cookie, off_t pos, void* buf, size_t* len);
static status_t device_write(void* cookie, off_t pos, const void* buf, size_t* len);

static device_hooks sDeviceHooks = {
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

static void
load_settings(void)
{
	void* handle = load_driver_settings("xgi.settings");
	if (handle != NULL) {
		gSettings.hardcursor = get_driver_boolean_parameter(
			handle, "hardcursor", gSettings.hardcursor, gSettings.hardcursor);
		gSettings.turboqueue = get_driver_boolean_parameter(
			handle, "turboqueue", gSettings.turboqueue, gSettings.turboqueue);

		const char* accelerant = get_driver_parameter(
			handle, "accelerant", gSettings.accelerant, gSettings.accelerant);
		if (accelerant != NULL)
			strlcpy(gSettings.accelerant, accelerant, sizeof(gSettings.accelerant));

		unload_driver_settings(handle);
	}
}

static area_id
map_physical(const char* name, phys_addr_t address, size_t size,
	uint32 allocSpec, uint32 protection, void** virtualAddress)
{
	*virtualAddress = NULL;
	size = (size + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1);
	return map_physical_memory(name, address, size, allocSpec, protection,
		virtualAddress);
}

static void
unlock_and_detect_vram(DeviceInfo& device, uint32& frameBufferSize)
{
	uint16 relIO = device.pciInfo.u.h0.base_registers[2] & 0xFFFC;
	uint16 srIndexPort = (relIO != 0) ? (relIO + 0x44) : 0x3C4;
	uint16 srDataPort = srIndexPort + 1;

	/* Unlock registers with key 0x86 in SR05 */
	out8(0x05, srIndexPort);
	out8(0x86, srDataPort);

	/* Read SR14 for memory configuration */
	out8(0x14, srIndexPort);
	uint8 memConfig = in8(srDataPort);

	uint32 detectedSize = 0;
	switch (memConfig >> 4) {
		case 8: detectedSize = 256 * 1024 * 1024; break;
		case 7: detectedSize = 128 * 1024 * 1024; break;
		case 6: detectedSize = 64 * 1024 * 1024; break;
		case 5: detectedSize = 32 * 1024 * 1024; break;
		case 4: detectedSize = 16 * 1024 * 1024; break;
		case 3: detectedSize = 8 * 1024 * 1024; break;
		default:
			detectedSize = 0;
			break;
	}

	if (detectedSize > 0)
		frameBufferSize = detectedSize;

	XGI_TRACE("SR14 memConfig=0x%02x -> detected VRAM: %" B_PRIu32 " MB\n",
		memConfig, frameBufferSize / (1024 * 1024));
}

static status_t
map_device(DeviceInfo& device)
{
	SharedInfo& shared = *device.sharedInfo;
	const pci_info& pciInfo = device.pciInfo;

	phys_addr_t fbBase = pciInfo.u.h0.base_registers[0];
	size_t fbSize = pciInfo.u.h0.base_register_sizes[0];
	phys_addr_t regsBase = pciInfo.u.h0.base_registers[1];
	size_t regsSize = pciInfo.u.h0.base_register_sizes[1];

	if (fbSize == 0)
		fbSize = 32 * 1024 * 1024;
	if (regsSize == 0)
		regsSize = 128 * 1024;

	uint32 vramSize = fbSize;
	unlock_and_detect_vram(device, vramSize);

	XGI_TRACE("map_device BAR0 fb=0x%" B_PRIxPHYSADDR " size=%" B_PRIu32
		" BAR1 regs=0x%" B_PRIxPHYSADDR " size=%" B_PRIu32 "\n",
		fbBase, (uint32)vramSize, regsBase, (uint32)regsSize);

	shared.regsArea = map_physical("xgi regs", regsBase, regsSize,
		B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA,
		(void**)&device.regs);
	if (shared.regsArea < B_OK)
		return shared.regsArea;

	shared.regsPCI = regsBase;
	shared.regsSize = regsSize;

	shared.frameBufferArea = map_physical("xgi framebuffer", fbBase,
		vramSize, B_ANY_KERNEL_BLOCK_ADDRESS | B_WRITE_COMBINING_MEMORY,
		B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA,
		(void**)&shared.frameBuffer);
	if (shared.frameBufferArea < B_OK) {
		shared.frameBufferArea = map_physical("xgi framebuffer", fbBase,
			vramSize, B_ANY_KERNEL_BLOCK_ADDRESS,
			B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA,
			(void**)&shared.frameBuffer);
	}
	if (shared.frameBufferArea < B_OK) {
		delete_area(shared.regsArea);
		shared.regsArea = -1;
		device.regs = NULL;
		return shared.frameBufferArea;
	}

	shared.frameBufferPCI = fbBase;
	shared.frameBufferSize = vramSize;
	shared.ioBase = pciInfo.u.h0.base_registers[2] & 0xFFFC;
	shared.ioSize = pciInfo.u.h0.base_register_sizes[2];

	/* Place hardware cursor at top 16KB of VRAM */
	shared.cursorBufferSize = 16384;
	shared.cursorOffset = (vramSize > shared.cursorBufferSize)
		? (vramSize - shared.cursorBufferSize) : 0;

	XGI_TRACE("map_device done: fbArea=%" B_PRId32 " regsArea=%" B_PRId32
		" cursorOffset=0x%" B_PRIx32 "\n",
		shared.frameBufferArea, shared.regsArea, shared.cursorOffset);

	return B_OK;
}

static void
unmap_device(DeviceInfo& device)
{
	SharedInfo& shared = *device.sharedInfo;
	XGI_TRACE("unmap_device %s\n", device.name);

	if (shared.regsArea >= B_OK)
		delete_area(shared.regsArea);
	if (shared.frameBufferArea >= B_OK)
		delete_area(shared.frameBufferArea);

	shared.regsArea = -1;
	shared.frameBufferArea = -1;
	shared.frameBuffer = NULL;
	device.regs = NULL;
}

static void
fill_boot_mode(SharedInfo& si)
{
	frame_buffer_boot_info* bootInfo = (frame_buffer_boot_info*)get_boot_item(
		FRAME_BUFFER_BOOT_INFO, NULL);
	if (bootInfo == NULL) {
		XGI_TRACE("fill_boot_mode: no FRAME_BUFFER_BOOT_INFO found\n");
		si.hasBootMode = false;
		return;
	}

	si.hasBootMode = true;
	si.bootWidth = bootInfo->width;
	si.bootHeight = bootInfo->height;
	si.bootDepth = bootInfo->depth;
	si.bootBytesPerRow = bootInfo->bytes_per_row;

	XGI_TRACE("fill_boot_mode: %" B_PRIu16 "x%" B_PRIu16 " depth=%" B_PRIu16
		" bpr=%" B_PRIu32 "\n",
		si.bootWidth, si.bootHeight, si.bootDepth, si.bootBytesPerRow);
}


#ifdef IS_PIRATI_BUILD
static void draw_logo(DeviceInfo& di)
{
    SharedInfo& si = *(di.sharedInfo);
    
    if (si.frameBufferArea < 0)
        return;

    // Retrieve framebuffer info from bootloader
    struct frame_buffer_boot_info* bi = (struct frame_buffer_boot_info*)get_boot_item(
        FRAME_BUFFER_BOOT_INFO, NULL);
    
    if (!bi)
        return;

    if (bi->depth != 32)
        return;

    uint32 screenWidth = bi->width;
    uint32 screenHeight = bi->height;
    
    uint32 bytesPerRow = bi->bytes_per_row;
    if (bytesPerRow == 0)
        bytesPerRow = screenWidth * 4;

    uint32 fbPitch = bytesPerRow / 4;

    uint32 logoW = kBitmapWidth;   // 1024
    uint32 logoH = kBitmapHeight;  // 571

    // Centering
    int32 startX = (int32)((screenWidth - logoW) / 2);
    if (startX < 0) startX = 0;
    int32 startY = (int32)((screenHeight - logoH) / 2);
    if (startY < 0) startY = 0;

    uint8* fb = si.frameBuffer;
    if (fb == NULL) {
        fb = (uint8*)bi->frame_buffer;
    }
    if (fb == NULL)
        return;

    for (uint32 y = 0; y < logoH && (startY + y) < screenHeight; y++) {
        // Offset di destinazione nel framebuffer (in byte)
        uint32 fbOffset = ((startY + y) * fbPitch + startX) * sizeof(uint32);
        
        // Offset di origine nell'array dell'immagine (4 byte per pixel in RGBA32)
        uint32 logoRowOffset = y * logoW * 4;
        
        uint32 remainingWidth = screenWidth - startX;
        uint32 copyPixels = (logoW < remainingWidth) ? logoW : remainingWidth;
        
        // Dimensione totale del blocco da copiare in byte per la riga
        uint32 copySize = copyPixels * 4; 

        // Copia sicura dalla memoria kernel allo spazio del framebuffer
        // (Sostituisci kkyro_ship_pngBits con il nome esatto definito nel tuo header)
        user_memcpy(fb + fbOffset, (void*)&kxgi_logo2_pngBits[logoRowOffset], copySize);
    }
}
#endif


static status_t
init_device(DeviceInfo& device)
{
	XGI_TRACE("init_device begin %s\n", device.name);

	char areaName[B_OS_NAME_LENGTH];
	snprintf(areaName, sizeof(areaName), "xgi_%04x_%04x_%02x%02x%02x shared",
		device.pciInfo.vendor_id, device.pciInfo.device_id, device.pciInfo.bus,
		device.pciInfo.device, device.pciInfo.function);

	device.sharedArea = create_area(areaName, (void**)&device.sharedInfo,
		B_ANY_KERNEL_ADDRESS,
		(sizeof(SharedInfo) + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1),
		B_FULL_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA);
	if (device.sharedArea < B_OK)
		return device.sharedArea;

	memset(device.sharedInfo, 0, sizeof(SharedInfo));
	device.sharedInfo->regsArea = -1;
	device.sharedInfo->frameBufferArea = -1;
	device.sharedInfo->modeArea = -1;
	device.sharedInfo->vendorID = device.pciInfo.vendor_id;
	device.sharedInfo->deviceID = device.pciInfo.device_id;
	device.sharedInfo->revision = device.pciInfo.revision;
	device.sharedInfo->bus = device.pciInfo.bus;
	device.sharedInfo->device = device.pciInfo.device;
	device.sharedInfo->function = device.pciInfo.function;
	device.sharedInfo->maxPixelClock = 350000;

	device.sharedInfo->colorSpaceCount = 3;
	device.sharedInfo->colorSpaces[0] = B_RGB32;
	device.sharedInfo->colorSpaces[1] = B_RGB16;
	device.sharedInfo->colorSpaces[2] = B_CMAP8;

	strlcpy(device.sharedInfo->deviceName, device.name,
		sizeof(device.sharedInfo->deviceName));

	/* Identify chip */
	device.sharedInfo->chipType = XGI_UNKNOWN;
	strlcpy(device.sharedInfo->chipName, "XGI Unknown",
		sizeof(device.sharedInfo->chipName));

	for (int i = 0; kSupportedChips[i].name != NULL; i++) {
		if (kSupportedChips[i].deviceID == device.pciInfo.device_id) {
			device.sharedInfo->chipType = kSupportedChips[i].chipType;
			strlcpy(device.sharedInfo->chipName, kSupportedChips[i].name,
				sizeof(device.sharedInfo->chipName));
			break;
		}
	}

	load_settings();
	memcpy(&device.sharedInfo->settings, &gSettings, sizeof(xgi_settings));

	/* Initialize 2D engine synchronization lock */
	device.sharedInfo->engine.lock.Init("xgi engine lock");
	device.sharedInfo->engine.count = 0;

	status_t status = map_device(device);
	if (status != B_OK) {
		delete_area(device.sharedArea);
		device.sharedArea = -1;
		device.sharedInfo = NULL;
		return status;
	}

	fill_boot_mode(*device.sharedInfo);

	/* Check for bootloader EDID */
	edid1_info* bootEdid = (edid1_info*)get_boot_item(VESA_EDID_BOOT_INFO, NULL);
	if (bootEdid != NULL) {
		memcpy(&device.sharedInfo->edidInfo, bootEdid, sizeof(edid1_info));
		device.sharedInfo->hasEdid = true;
		XGI_TRACE("init_device: bootloader EDID found\n");
	} else {
		device.sharedInfo->hasEdid = false;
		XGI_TRACE("init_device: no bootloader EDID available\n");
	}
#ifdef IS_PIRATI_BUILD
	draw_logo(device);
	snooze(2000000);
#endif
	return B_OK;
}

static void
uninit_device(DeviceInfo& device)
{
	XGI_TRACE("uninit_device %s\n", device.name);
	if (device.sharedInfo != NULL) {
		device.sharedInfo->engine.lock.Destroy();
	}
	unmap_device(device);
	if (device.sharedArea >= B_OK)
		delete_area(device.sharedArea);

	device.sharedArea = -1;
	device.sharedInfo = NULL;
}

status_t
init_hardware(void)
{
	XGI_TRACE("init_hardware begin\n");
	if (get_module(B_PCI_MODULE_NAME, (module_info**)&gPCI) != B_OK)
		return B_ERROR;

	bool found = false;
	pci_info pciInfo;
	for (int32 index = 0; gPCI->get_nth_pci_info(index, &pciInfo) == B_OK; index++) {
		if (pciInfo.vendor_id != PCI_VENDOR_XGI)
			continue;

		for (int i = 0; kSupportedChips[i].name != NULL; i++) {
			if (kSupportedChips[i].deviceID == pciInfo.device_id) {
				found = true;
				XGI_TRACE("init_hardware: found %s (0x%04x:0x%04x)\n",
					kSupportedChips[i].name, pciInfo.vendor_id, pciInfo.device_id);
				break;
			}
		}
		if (found)
			break;
	}

	put_module(B_PCI_MODULE_NAME);
	XGI_TRACE("init_hardware result=%d\n", found);
	return found ? B_OK : ENODEV;
}

status_t
init_driver(void)
{
	XGI_TRACE("init_driver begin\n");
	if (get_module(B_PCI_MODULE_NAME, (module_info**)&gPCI) != B_OK)
		return B_ERROR;

	load_settings();

	uint32 count = 0;
	pci_info pciInfo;
	for (int32 index = 0; count < MAX_DEVICES
			&& gPCI->get_nth_pci_info(index, &pciInfo) == B_OK; index++) {
		if (pciInfo.vendor_id != PCI_VENDOR_XGI)
			continue;

		bool supported = false;
		for (int i = 0; kSupportedChips[i].name != NULL; i++) {
			if (kSupportedChips[i].deviceID == pciInfo.device_id) {
				supported = true;
				break;
			}
		}
		if (!supported)
			continue;

		DeviceInfo& device = gDeviceInfo[count];
		memset(&device, 0, sizeof(DeviceInfo));
		device.sharedArea = -1;
		device.sharedInfo = NULL;
		device.regs = NULL;
		device.pciInfo = pciInfo;

		snprintf(device.name, sizeof(device.name), "graphics/%04x_%04x_%02x%02x%02x",
			pciInfo.vendor_id, pciInfo.device_id, pciInfo.bus, pciInfo.device,
			pciInfo.function);

		XGI_TRACE("init_driver found device[%u]=%s rev=0x%02x\n",
			(unsigned int)count, device.name, pciInfo.revision);

		gDeviceNames[count] = device.name;
		count++;
	}

	gDeviceNames[count] = NULL;
	if (count == 0) {
		XGI_TRACE("init_driver: no supported XGI devices found\n");
		put_module(B_PCI_MODULE_NAME);
		return ENODEV;
	}

	XGI_TRACE("init_driver done count=%u\n", (unsigned int)count);
	return B_OK;
}

void
uninit_driver(void)
{
	XGI_TRACE("uninit_driver\n");
	for (uint32 index = 0; index < MAX_DEVICES; index++) {
		if (gDeviceInfo[index].sharedArea >= B_OK)
			uninit_device(gDeviceInfo[index]);
	}
	put_module(B_PCI_MODULE_NAME);
}

const char**
publish_devices(void)
{
	return gDeviceNames;
}

device_hooks*
find_device(const char* name)
{
	for (uint32 index = 0; gDeviceNames[index] != NULL; index++) {
		if (strcmp(name, gDeviceNames[index]) == 0)
			return &sDeviceHooks;
	}
	return NULL;
}

static status_t
device_open(const char* name, uint32 flags, void** cookie)
{
	(void)flags;
	DeviceInfo* device = NULL;
	for (uint32 index = 0; gDeviceNames[index] != NULL; index++) {
		if (strcmp(name, gDeviceInfo[index].name) == 0) {
			device = &gDeviceInfo[index];
			break;
		}
	}
	if (device == NULL)
		return B_ENTRY_NOT_FOUND;

	if (device->openCount == 0) {
		/* Enable PCI Bus Mastering, Memory Space, and I/O Space */
		uint16 command = gPCI->read_pci_config(device->pciInfo.bus,
			device->pciInfo.device, device->pciInfo.function, PCI_command, 2);
		command |= (PCI_command_io | PCI_command_memory | PCI_command_master);
		gPCI->write_pci_config(device->pciInfo.bus, device->pciInfo.device,
			device->pciInfo.function, PCI_command, 2, command);

		status_t status = init_device(*device);
		if (status != B_OK)
			return status;
	}

	device->openCount++;
	*cookie = device;
	XGI_TRACE("device_open: %s (openCount=%u)\n", name, (unsigned int)device->openCount);
	return B_OK;
}

static status_t
device_close(void* cookie)
{
	(void)cookie;
	return B_OK;
}

static status_t
device_free(void* cookie)
{
	DeviceInfo* device = (DeviceInfo*)cookie;
	if (device == NULL)
		return B_BAD_VALUE;

	if (device->openCount > 0)
		device->openCount--;

	XGI_TRACE("device_free: %s (openCount=%u)\n", device->name,
		(unsigned int)device->openCount);

	if (device->openCount == 0)
		uninit_device(*device);

	return B_OK;
}

static status_t
device_ioctl(void* cookie, uint32 msg, void* buf, size_t len)
{
	DeviceInfo* device = (DeviceInfo*)cookie;
	if (device == NULL)
		return B_BAD_VALUE;

	switch (msg) {
		case XGI_GET_PRIVATE_DATA: {
			XgiGetPrivateData gpd;
			if (user_memcpy(&gpd, buf, sizeof(gpd)) != B_OK)
				return B_BAD_ADDRESS;
			if (gpd.magic != XGI_PRIVATE_DATA_MAGIC)
				return B_BAD_VALUE;
			gpd.sharedInfoArea = device->sharedArea;
			return user_memcpy(buf, &gpd, sizeof(gpd));
		}

		case B_GET_ACCELERANT_SIGNATURE: {
			if (user_strlcpy((char*)buf, device->sharedInfo->settings.accelerant, len) < B_OK)
				return B_BAD_ADDRESS;
			return B_OK;
		}

		case XGI_DEVICE_NAME: {
			if (user_strlcpy((char*)buf, device->name, len) < B_OK)
				return B_BAD_ADDRESS;
			return B_OK;
		}

		case XGI_GET_EDID: {
			if (!device->sharedInfo->hasEdid)
				return B_NAME_NOT_FOUND;
			if (len < sizeof(edid1_info))
				return B_BUFFER_OVERFLOW;
			return user_memcpy(buf, &device->sharedInfo->edidInfo, sizeof(edid1_info));
		}

		default:
			return B_DEV_INVALID_IOCTL;
	}
}

static status_t
device_read(void* cookie, off_t pos, void* buf, size_t* len)
{
	(void)cookie;
	(void)pos;
	(void)buf;
	*len = 0;
	return B_NOT_ALLOWED;
}

static status_t
device_write(void* cookie, off_t pos, const void* buf, size_t* len)
{
	(void)cookie;
	(void)pos;
	(void)buf;
	*len = 0;
	return B_NOT_ALLOWED;
}
