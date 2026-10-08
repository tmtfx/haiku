/*
 * Copyright 2026, GitHub Copilot. All rights reserved.
 * Distributed under the terms of the MIT License.
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


#define KYRO_VENDOR_ID					0x104a
#define KYRO_DEVICE_ID_STG4000			0x0010
#define MAX_DEVICES						4
#define DEVICE_FORMAT					"%04x_%04x_%02x%02x%02x"

#define PCI_CONFIG_SUBSYS_ID			0x2e
#define CORE_PLL_CONTROL				0x70

#define REG_THREAD0_ENABLE				0x0000
#define REG_THREAD1_ENABLE				0x0004
#define REG_SOFTWARE_RESET				0x0080
#define REG_INT_MASK					0x0130
#define REG_TA_CONFIGURATION			0x0818
#define REG_SDRAM_ARBITER_CONF			0x0c60
#define REG_SDRAM_CONF0					0x0c64
#define REG_SDRAM_CONF1					0x0c68
#define REG_SDRAM_CONF2					0x0c6c
#define REG_SDRAM_REFRESH				0x0c70

#define PMX2_SOFTRESET_DAC_RST			0x0001
#define PMX2_SOFTRESET_C1_RST			0x0004
#define PMX2_SOFTRESET_C2_RST			0x0008
#define PMX2_SOFTRESET_3D_RST			0x0010
#define PMX2_SOFTRESET_VIDIN_RST		0x0020
#define PMX2_SOFTRESET_TLB_RST			0x0040
#define PMX2_SOFTRESET_SD_RST			0x0080
#define PMX2_SOFTRESET_VGA_RST			0x0100
#define PMX2_SOFTRESET_ROM_RST			0x0200
#define PMX2_SOFTRESET_TA_RST			0x0400
#define PMX2_SOFTRESET_REG_RST			0x4000
#define PMX2_SOFTRESET_ALL				0x7fff


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
static kyro_settings	gSettings = {
	KYRO_ACCELERANT_NAME,
	false,
	0
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


static inline uint32
read_reg(DeviceInfo& device, uint32 offset)
{
	return *(volatile uint32*)(device.regs + offset);
}


static inline void
write_reg(DeviceInfo& device, uint32 offset, uint32 value)
{
	*(volatile uint32*)(device.regs + offset) = value;
}


static uint32
program_clock(uint32 refClock, uint32 coreClock, uint32& feedbackOut,
	uint32& dividerOut, uint32& postDividerOut)
{
	static const uint32 kScaler = 8;
	static const uint32 kMinR = 2;
	static const uint32 kMaxR = 33;
	static const uint32 kMinF = 2;
	static const uint32 kMaxF = 513;
	static const uint32 kMinRestrictedVco = 100000000;
	static const uint32 kMaxRestrictedVco = 500000000;
	static const uint32 kMaxVco = 500000000;
	static const uint32 kOdValues[] = {1, 2, 0};

	uint32 bestClock = 0;
	uint32 bestScore = 0;
	uint32 bestR = 0;
	uint32 bestF = 0;
	uint32 bestOd = 0;

	uint32 requestedHz = coreClock * 100;
	uint32 refHz = refClock * 1000;
	uint32 minClock = requestedHz - (requestedHz >> 8);
	uint32 maxClock = requestedHz + (requestedHz >> 8);
	uint32 scaledRequest = requestedHz >> kScaler;

	for (uint32 odIndex = 0; odIndex < 3; odIndex++) {
		uint32 od = kOdValues[odIndex];
		for (uint32 r = kMinR; r <= kMaxR; r++) {
			uint32 scaled = r * (scaledRequest << od);
			uint32 f = scaled / (refHz >> kScaler);
			if (f > kMinF)
				f--;

			while (f >= kMinF && f <= kMaxF) {
				uint64 vco = (uint64)refHz * f / r;
				if (vco >= kMinRestrictedVco
					&& (vco <= kMaxRestrictedVco
						|| (requestedHz > kMaxRestrictedVco && vco <= kMaxVco))) {
					uint32 clock = (uint32)(vco >> od);
					if (clock >= minClock && clock <= maxClock) {
						uint32 phaseScore = (((refHz / r) - (refHz / kMaxR))
							/ ((refHz - (refHz / kMaxR)) >> 10));
						uint32 vcoScore = ((vco - kMinRestrictedVco)
							/ ((kMaxRestrictedVco - kMinRestrictedVco) >> 10));
						uint32 score = phaseScore + vcoScore;

						if (bestScore == 0 || (score >= bestScore && od > 0)) {
							bestScore = score;
							bestClock = clock;
							bestR = r;
							bestF = f;
							bestOd = od;
						}
					}
				}
				f++;
			}
		}
	}

	if (bestScore == 0)
		return 0;

	dividerOut = bestR;
	feedbackOut = bestF;
	postDividerOut = (bestOd == 2 || bestOd == 3) ? 3 : bestOd;
	return bestClock;
}


static uint32
init_sdram_registers(DeviceInfo& device, uint32 subSystemId, uint32 revision)
{
	static const uint8 kSdramArbiter[] = {0xa0, 0x80, 0xa0, 0xa0, 0xa0};
	static const uint16 kSdramCfg1[] = {0x8732, 0x8732, 0xa732, 0xa732, 0x8732};
	static const uint16 kSdramCfg2[] = {0x87d2, 0x87d2, 0xa7d2, 0x87d2, 0xa7d2};
	static const uint8 kRefresh[] = {36, 39, 40};
	static const uint8 kChipSpeed[] = {110, 120, 125};

	uint32 memTypeIndex = (subSystemId & 0x70) >> 4;
	uint32 chipSpeedIndex = (subSystemId & 0x180) >> 7;
	if (memTypeIndex > 4 || chipSpeedIndex > 2)
		return 0;

	write_reg(device, REG_SDRAM_ARBITER_CONF, kSdramArbiter[memTypeIndex]);
	if (revision < 5) {
		write_reg(device, REG_SDRAM_CONF0, 0x49a1);
		write_reg(device, REG_SDRAM_CONF1, kSdramCfg1[memTypeIndex]);
	} else {
		write_reg(device, REG_SDRAM_CONF0, 0x4df1);
		write_reg(device, REG_SDRAM_CONF1, kSdramCfg2[memTypeIndex]);
	}

	write_reg(device, REG_SDRAM_CONF2, 0x31);
	write_reg(device, REG_SDRAM_REFRESH, kRefresh[chipSpeedIndex]);
	return kChipSpeed[chipSpeedIndex] * 10000;
}


static void
busy_delay(uint32 iterations)
{
	volatile uint32 count = 0;
	for (uint32 i = 0; i < iterations; i++)
		count++;
	(void)count;
}


static status_t
initialize_chip(DeviceInfo& device)
{
	write_reg(device, REG_INT_MASK, 0xffff);

	uint32 value = read_reg(device, REG_THREAD0_ENABLE);
	value &= ~(1u << 0);
	write_reg(device, REG_THREAD0_ENABLE, value);

	value = read_reg(device, REG_THREAD1_ENABLE);
	value &= ~(1u << 0);
	write_reg(device, REG_THREAD1_ENABLE, value);

	write_reg(device, REG_SOFTWARE_RESET,
		PMX2_SOFTRESET_REG_RST | PMX2_SOFTRESET_ROM_RST);
	write_reg(device, REG_SOFTWARE_RESET,
		PMX2_SOFTRESET_REG_RST | PMX2_SOFTRESET_TA_RST
			| PMX2_SOFTRESET_ROM_RST);

	write_reg(device, REG_TA_CONFIGURATION, 0);
	write_reg(device, REG_SOFTWARE_RESET,
		PMX2_SOFTRESET_REG_RST | PMX2_SOFTRESET_ROM_RST);
	write_reg(device, REG_SOFTWARE_RESET,
		PMX2_SOFTRESET_REG_RST | PMX2_SOFTRESET_TA_RST
			| PMX2_SOFTRESET_ROM_RST);

	uint16 subSystemId = gPCI->read_pci_config(device.pciInfo.bus,
		device.pciInfo.device, device.pciInfo.function, PCI_CONFIG_SUBSYS_ID, 2);
	uint32 chipSpeed = init_sdram_registers(device, subSystemId,
		device.pciInfo.revision);
	if (chipSpeed == 0)
		return B_ERROR;

	uint32 feedback;
	uint32 divider;
	uint32 postDivider;
	if (program_clock(14318, 1000000, feedback, divider, postDivider) == 0)
		return B_ERROR;

	uint16 corePll = (uint16)(postDivider | ((feedback - 2) << 2)
		| ((divider - 2) << 11));

	uint16 config = (3u << 8) | (corePll & 0x00ff);
	gPCI->write_pci_config(device.pciInfo.bus, device.pciInfo.device,
		device.pciInfo.function, CORE_PLL_CONTROL, 2, config);
	busy_delay(1000000);

	config |= 1u << 14;
	gPCI->write_pci_config(device.pciInfo.bus, device.pciInfo.device,
		device.pciInfo.function, CORE_PLL_CONTROL, 2, config);
	busy_delay(1000000);

	config = (2u << 8) | ((corePll & 0xff00) >> 8);
	gPCI->write_pci_config(device.pciInfo.bus, device.pciInfo.device,
		device.pciInfo.function, CORE_PLL_CONTROL, 2, config);
	busy_delay(1000000);

	config |= 1u << 14;
	gPCI->write_pci_config(device.pciInfo.bus, device.pciInfo.device,
		device.pciInfo.function, CORE_PLL_CONTROL, 2, config);
	busy_delay(1000000);

	write_reg(device, REG_SOFTWARE_RESET, PMX2_SOFTRESET_ALL);
	return B_OK;
}


static area_id
map_physical(const char* name, phys_addr_t physical, uint32 size,
	uint32 flags, uint32 protection, void** virtualAddress)
{
	void* address = NULL;
	size = (size + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1);

	area_id area = map_physical_memory(name, physical, size, flags, protection,
		&address);
	if (area >= B_OK)
		*virtualAddress = address;
	else
		*virtualAddress = NULL;

	return area;
}


static void
load_settings(void)
{
	void* handle = load_driver_settings("kyro.settings");
	if (handle == NULL)
		return;

	unload_driver_settings(handle);
}


static void
fill_boot_mode(SharedInfo& si)
{
	frame_buffer_boot_info* bootInfo = (frame_buffer_boot_info*)get_boot_item(
		FRAME_BUFFER_BOOT_INFO, NULL);
	if (bootInfo == NULL) {
		si.hasBootMode = false;
		return;
	}

	si.hasBootMode = true;
	si.bootWidth = bootInfo->width;
	si.bootHeight = bootInfo->height;
	si.bootDepth = bootInfo->depth;
}


static status_t
map_device(DeviceInfo& device)
{
	pci_info& pciInfo = device.pciInfo;
	SharedInfo& shared = *device.sharedInfo;

	gPCI->write_pci_config(pciInfo.bus, pciInfo.device, pciInfo.function,
		PCI_command, 2, gPCI->read_pci_config(pciInfo.bus, pciInfo.device,
			pciInfo.function, PCI_command, 2)
			| PCI_command_io | PCI_command_memory | PCI_command_master);

	uint32 frameBufferBase = pciInfo.u.h0.base_registers[0] & ~0x0f;
	uint32 frameBufferSize = pciInfo.u.h0.base_register_sizes[0];
	uint32 regsBase = pciInfo.u.h0.base_registers[1] & ~0x0f;
	uint32 regsSize = pciInfo.u.h0.base_register_sizes[1];

	if (frameBufferSize == 0)
		frameBufferSize = 16 * 1024 * 1024;
	if (regsSize == 0)
		regsSize = 128 * 1024;

	shared.regsArea = map_physical("kyro regs", regsBase, regsSize,
		B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA,
		(void**)&device.regs);
	if (shared.regsArea < B_OK)
		return shared.regsArea;

	shared.frameBufferArea = map_physical("kyro framebuffer", frameBufferBase,
		frameBufferSize, B_ANY_KERNEL_BLOCK_ADDRESS | B_WRITE_COMBINING_MEMORY,
		B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA,
		(void**)&shared.frameBuffer);
	if (shared.frameBufferArea < B_OK) {
		shared.frameBufferArea = map_physical("kyro framebuffer", frameBufferBase,
			frameBufferSize, B_ANY_KERNEL_BLOCK_ADDRESS,
			B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA,
			(void**)&shared.frameBuffer);
	}
	if (shared.frameBufferArea < B_OK) {
		delete_area(shared.regsArea);
		shared.regsArea = -1;
		device.regs = NULL;
		return shared.frameBufferArea;
	}

	shared.frameBufferPCI = pciInfo.u.h0.base_registers_pci[0] & ~0x0f;
	shared.frameBufferSize = frameBufferSize;
	return B_OK;
}


static void
unmap_device(DeviceInfo& device)
{
	SharedInfo& shared = *device.sharedInfo;

	if (shared.regsArea >= B_OK)
		delete_area(shared.regsArea);
	if (shared.frameBufferArea >= B_OK)
		delete_area(shared.frameBufferArea);

	shared.regsArea = -1;
	shared.frameBufferArea = -1;
	shared.frameBuffer = 0;
	device.regs = NULL;
}


static status_t
init_device(DeviceInfo& device)
{
	char areaName[B_OS_NAME_LENGTH];
	snprintf(areaName, sizeof(areaName), DEVICE_FORMAT " shared",
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
	device.sharedInfo->maxPixelClock = 270000;
	device.sharedInfo->colorSpaceCount = 2;
	device.sharedInfo->colorSpaces[0] = B_RGB16;
	device.sharedInfo->colorSpaces[1] = B_RGB32;
	device.sharedInfo->dpmsMode = B_DPMS_ON;
	device.sharedInfo->cursorBufferSize = 1024;
	strlcpy(device.sharedInfo->chipName, "PowerVR Kyro/Kyro II",
		sizeof(device.sharedInfo->chipName));
	strlcpy(device.sharedInfo->deviceName, device.name,
		sizeof(device.sharedInfo->deviceName));
	memcpy(&device.sharedInfo->settings, &gSettings, sizeof(gSettings));
	fill_boot_mode(*device.sharedInfo);

	status_t status = map_device(device);
	if (status < B_OK) {
		delete_area(device.sharedArea);
		device.sharedArea = -1;
		device.sharedInfo = NULL;
		return status;
	}

	status = initialize_chip(device);
	if (status != B_OK) {
		unmap_device(device);
		delete_area(device.sharedArea);
		device.sharedArea = -1;
		device.sharedInfo = NULL;
		return status;
	}

	device.sharedInfo->cursorOffset
		= device.sharedInfo->frameBufferSize > device.sharedInfo->cursorBufferSize
			? device.sharedInfo->frameBufferSize - device.sharedInfo->cursorBufferSize
			: 0;

	edid1_info* bootEdid = (edid1_info*)get_boot_item(VESA_EDID_BOOT_INFO, NULL);
	if (bootEdid != NULL) {
		device.sharedInfo->hasEdid = true;
		memcpy(&device.sharedInfo->edidInfo, bootEdid, sizeof(edid1_info));
	} else
		device.sharedInfo->hasEdid = false;

	return B_OK;
}


status_t
init_hardware(void)
{
	if (get_module(B_PCI_MODULE_NAME, (module_info**)&gPCI) != B_OK)
		return B_ERROR;

	bool found = false;
	pci_info pciInfo;
	for (int32 index = 0; gPCI->get_nth_pci_info(index, &pciInfo) == B_OK; index++) {
		if (pciInfo.vendor_id == KYRO_VENDOR_ID
			&& pciInfo.device_id == KYRO_DEVICE_ID_STG4000) {
			found = true;
			break;
		}
	}

	put_module(B_PCI_MODULE_NAME);
	return found ? B_OK : B_ERROR;
}


status_t
init_driver(void)
{
	if (get_module(B_PCI_MODULE_NAME, (module_info**)&gPCI) != B_OK)
		return B_ERROR;

	load_settings();

	uint32 count = 0;
	pci_info pciInfo;
	for (int32 index = 0; count < MAX_DEVICES
			&& gPCI->get_nth_pci_info(index, &pciInfo) == B_OK; index++) {
		if (pciInfo.vendor_id != KYRO_VENDOR_ID
			|| pciInfo.device_id != KYRO_DEVICE_ID_STG4000) {
			continue;
		}

		DeviceInfo& device = gDeviceInfo[count];
		memset(&device, 0, sizeof(DeviceInfo));
		device.sharedArea = -1;
		device.sharedInfo = NULL;
		device.regs = NULL;
		device.pciInfo = pciInfo;
		snprintf(device.name, sizeof(device.name), "graphics/" DEVICE_FORMAT,
			pciInfo.vendor_id, pciInfo.device_id, pciInfo.bus, pciInfo.device,
			pciInfo.function);
		gDeviceNames[count] = device.name;
		count++;
	}

	gDeviceNames[count] = NULL;
	if (count == 0) {
		put_module(B_PCI_MODULE_NAME);
		return B_ERROR;
	}

	return B_OK;
}


void
uninit_driver(void)
{
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
	for (int32 i = 0; gDeviceNames[i] != NULL; i++) {
		if (strcmp(name, gDeviceNames[i]) == 0)
			return &sDeviceHooks;
	}
	return NULL;
}


static status_t
device_open(const char* name, uint32 flags, void** cookie)
{
	(void)flags;
	for (int32 i = 0; gDeviceNames[i] != NULL; i++) {
		DeviceInfo& device = gDeviceInfo[i];
		if (strcmp(name, device.name) != 0)
			continue;

		if (device.openCount == 0) {
			status_t status = init_device(device);
			if (status != B_OK)
				return status;
		}

		device.openCount++;
		*cookie = &device;
		return B_OK;
	}

	return B_BAD_VALUE;
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
	DeviceInfo& device = *(DeviceInfo*)cookie;
	if (device.openCount == 0)
		return B_BAD_VALUE;

	if (--device.openCount == 0) {
		unmap_device(device);
		delete_area(device.sharedArea);
		device.sharedArea = -1;
		device.sharedInfo = NULL;
	}

	return B_OK;
}


static status_t
device_ioctl(void* cookie, uint32 msg, void* buf, size_t len)
{
	DeviceInfo& device = *(DeviceInfo*)cookie;
	(void)len;

	switch (msg) {
		case B_GET_ACCELERANT_SIGNATURE:
			if (user_strlcpy((char*)buf, KYRO_ACCELERANT_NAME,
					B_FILE_NAME_LENGTH) < B_OK) {
				return B_BAD_ADDRESS;
			}
			return B_OK;

		case KYRO_DEVICE_NAME:
			if (user_strlcpy((char*)buf, device.name, B_PATH_NAME_LENGTH) < B_OK)
				return B_BAD_ADDRESS;
			return B_OK;

		case KYRO_GET_PRIVATE_DATA:
		{
			KyroGetPrivateData data;
			if (user_memcpy(&data, buf, sizeof(data)) != B_OK)
				return B_BAD_ADDRESS;
			if (data.magic != KYRO_PRIVATE_DATA_MAGIC)
				return B_BAD_VALUE;

			data.sharedInfoArea = device.sharedArea;
			return user_memcpy(buf, &data, sizeof(data));
		}

		case KYRO_GET_EDID:
			if (!device.sharedInfo->hasEdid)
				return B_ERROR;
			return user_memcpy(buf, &device.sharedInfo->edidInfo,
				sizeof(device.sharedInfo->edidInfo));
	}

	return B_DEV_INVALID_IOCTL;
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
