/*
 * Copyright 1992-2003, Alan Hourihane. All rights reserved.
 * Copyright 2026, Gemini CLI. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Alan Hourihane <alanh@fairlite.demon.co.uk>
 *		Fabio Tomat <f.t.public@gmail.com>
 *		Gemini CLI <gemini-cli@google.com>
 */


#include <KernelExport.h>
#include <PCI.h>
#ifdef __HAIKU__
#include <drivers/bios.h>
#endif
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <graphic_driver.h>

#include <boot_item.h>
#include <frame_buffer_console.h>

#include "DriverInterface.h"


#undef TRACE

#ifdef ENABLE_DEBUG_TRACE
#	define TRACE(x...) dprintf("Trident: " x)
#else
#	define TRACE(x...) ;
#endif


#define SKD_HANDLER_INSTALLED 0x80000000
#define MAX_DEVICES		4
#define DEVICE_FORMAT	"%04X_%04X_%02X%02X%02X"

int32 api_version = B_CUR_DRIVER_API_VERSION;

#define VENDOR_ID 0x1023


struct ChipInfo {
	uint16		chipID;
	uint16		chipType;
	const char*	chipName;
};


static const ChipInfo chipTable[] = {
	{ 0x9880, TRIDENT_BLADE3D, "Trident Blade3D" },
	{ 0,	  0,			   NULL }
};


struct DeviceInfo {
	uint32			openCount;
	int32			flags;
	area_id 		sharedArea;
	SharedInfo* 	sharedInfo;
	vuint8*	 		regs;
	const ChipInfo*	pChipInfo;
	pci_info		pciInfo;
	char			name[B_OS_NAME_LENGTH];
};


static Benaphore		gLock;
static DeviceInfo		gDeviceInfo[MAX_DEVICES];
static char*			gDeviceNames[MAX_DEVICES + 1];
static pci_module_info*	gPCI;


// Prototypes
static status_t device_open(const char* name, uint32 flags, void** cookie);
static status_t device_close(void* dev);
static status_t device_free(void* dev);
static status_t device_read(void* dev, off_t pos, void* buf, size_t* len);
static status_t device_write(void* dev, off_t pos, const void* buf, size_t* len);
static status_t device_ioctl(void* dev, uint32 msg, void* buf, size_t len);


static device_hooks gDeviceHooks =
{
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
GetPCI(pci_info& info, uint8 offset, uint8 size)
{
	return gPCI->read_pci_config(info.bus, info.device, info.function, offset, size);
}


static inline void
SetPCI(pci_info& info, uint8 offset, uint8 size, uint32 value)
{
	gPCI->write_pci_config(info.bus, info.device, info.function, offset, size, value);
}


static bool
InterruptIsVBI()
{
	return false;
}


static void
ClearVBI()
{
}


static void
EnableVBI()
{
}


static void
DisableVBI()
{
}


inline void
outb(uint16 port, uint8 value)
{
	__asm__ __volatile__ ("outb %0, %1" : : "a"(value), "Nd"(port));
}

inline uint8
inb(uint16 port)
{
	uint8 value;
	__asm__ __volatile__ ("inb %1, %0" : "=a"(value) : "Nd"(port));
	return value;
}

static void
EnableMMIO(DeviceInfo& di)
{
	uint16 vgaIOBase = (inb(0x3CC) & 0x01) ? 0x3D4 : 0x3B4;

	// Toggle Trident "New Mode" via legacy Port 0x3C4
	outb(0x3C4, 0x0B);
	(void)inb(0x3C5);

	// Unlock Extended Sequencer registers (SR0E = 0x80)
	outb(0x3C4, 0x0E);
	outb(0x3C5, 0x80);

	// Unlock CyberBlade/Blade3D-specific registers (SR11 = 0x92)
	outb(0x3C4, 0x11);
	outb(0x3C5, 0x92);

	// Enable hardware-level MMIO decoder on the Trident card (CR39 = 0x81)
	// Writing 0x81 simultaneously unlocks CRTC extensions (0x80) and enables MMIO (0x01)
	outb(vgaIOBase, 0x39);
	outb(vgaIOBase + 1, 0x81);

	// Readback and log to verify
	outb(vgaIOBase, 0x39);
	uint8 readback = inb(vgaIOBase + 1);
	dprintf("Trident: EnableMMIO: CR39 write=0x81, readback=0x%02X (I/O Port Base=0x%04X)\n", readback, vgaIOBase);
}


static status_t
MapDevice(DeviceInfo& di)
{
	char areaName[B_OS_NAME_LENGTH];
	SharedInfo& si = *(di.sharedInfo);
	pci_info& pciInfo = di.pciInfo;

	TRACE("enter MapDevice()\n");

	// Enable memory mapped IO and bus master
	SetPCI(pciInfo, PCI_command, 2, GetPCI(pciInfo, PCI_command, 2)
		| PCI_command_io | PCI_command_memory | PCI_command_master);

	// BAR 0 is Frame Buffer
	uint32 videoRamAddr = pciInfo.u.h0.base_registers[0] & ~0x0F;
	uint32 videoRamSize = pciInfo.u.h0.base_register_sizes[0];
	si.videoMemPCI = pciInfo.u.h0.base_registers_pci[0] & ~0x0F;

	if (videoRamSize == 0)
		videoRamSize = 8 * 1024 * 1024; // fallback to 8MB if undetected

	// BAR 1 is MMIO registers - mask lower 14 bits to align to 16KB and strip PCI status flags, matching X.org exactly
	uint32 regsBase = pciInfo.u.h0.base_registers[1] & 0xFFFFC000;
	uint32 regAreaSize = pciInfo.u.h0.base_register_sizes[1];

	if (regAreaSize == 0)
		regAreaSize = 128 * 1024; // fallback to 128KB if undetected
		
	dprintf("Trident 9880 Driver Debug:\n");
	dprintf("  BAR 0 (Framebuffer): Base = 0x%08" B_PRIx32 ", Size = %" B_PRIu32 " KB (%" B_PRIu32 " MB)\n", 
			videoRamAddr, videoRamSize / 1024, videoRamSize / (1024 * 1024));

	dprintf("  BAR 1 (MMIO Regs)  : Base = 0x%08" B_PRIx32 ", Size = %" B_PRIu32 " KB (%" B_PRIu32 " Bytes)\n", 
			regsBase, regAreaSize / 1024, regAreaSize);

	// Verifica dei flag PCI (Memoria vs I/O e Prefetchable)
	dprintf("  BAR 0 Flags: 0x%02X, BAR 1 Flags: 0x%02X\n",
			pciInfo.u.h0.base_register_flags[0],
			pciInfo.u.h0.base_register_flags[1]);

	// Map MMIO
	sprintf(areaName, DEVICE_FORMAT " regs",
		pciInfo.vendor_id, pciInfo.device_id,
		pciInfo.bus, pciInfo.device, pciInfo.function);

	si.regsArea = map_physical_memory(areaName, regsBase, regAreaSize,
		B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA,
		(void**)(&(di.regs)));

	if (si.regsArea < 0)
		return si.regsArea;

	// Map Frame Buffer
	sprintf(areaName, DEVICE_FORMAT " framebuffer",
		pciInfo.vendor_id, pciInfo.device_id,
		pciInfo.bus, pciInfo.device, pciInfo.function);

	si.videoMemSize = videoRamSize;
	si.videoMemArea = map_physical_memory(
		areaName,
		videoRamAddr,
		videoRamSize,
		B_ANY_KERNEL_BLOCK_ADDRESS | B_WRITE_COMBINING_MEMORY,
		B_READ_AREA + B_WRITE_AREA,
		(void**)(&(si.videoMemAddr)));

	if (si.videoMemArea < 0) {
		si.videoMemArea = map_physical_memory(
			areaName,
			videoRamAddr,
			videoRamSize,
			B_ANY_KERNEL_BLOCK_ADDRESS,
			B_READ_AREA + B_WRITE_AREA,
			(void**)(&(si.videoMemAddr)));
	}

	if (si.videoMemArea < 0) {
		delete_area(si.regsArea);
		si.regsArea = -1;
		return si.videoMemArea;
	}

	// Enable hardware-level MMIO decoder on the Trident card
	EnableMMIO(di);

	TRACE("Video memory mapped at area: %d, addr: 0x%" B_PRIXADDR "\n",
		si.videoMemArea, (addr_t)(si.videoMemAddr));

	return B_OK;
}


static void
UnmapDevice(DeviceInfo& di)
{
	SharedInfo& si = *(di.sharedInfo);

	TRACE("enter UnmapDevice()\n");

	if (si.regsArea >= 0)
		delete_area(si.regsArea);
	if (si.videoMemArea >= 0)
		delete_area(si.videoMemArea);

	si.regsArea = si.videoMemArea = -1;
	si.videoMemAddr = 0;
	di.regs = NULL;

	TRACE("exit UnmapDevice()\n");
}


static int32
InterruptHandler(void* data)
{
	int32 handled = B_UNHANDLED_INTERRUPT;
	DeviceInfo& di = *((DeviceInfo*)data);
	int32* flags = &(di.flags);

	if (atomic_or(flags, SKD_HANDLER_INSTALLED) & SKD_HANDLER_INSTALLED)
		return B_UNHANDLED_INTERRUPT;

	if (InterruptIsVBI()) {
		ClearVBI();
		handled = B_HANDLED_INTERRUPT;

		sem_id& sem = di.sharedInfo->vertBlankSem;
		if (sem >= 0) {
			int32 blocked;
			if ((get_sem_count(sem, &blocked) == B_OK) && (blocked < 0)) {
				release_sem_etc(sem, -blocked, B_DO_NOT_RESCHEDULE);
				handled = B_INVOKE_SCHEDULER;
			}
		}
	}

	atomic_and(flags, ~SKD_HANDLER_INSTALLED);
	return handled;
}


static void
InitInterruptHandler(DeviceInfo& di)
{
	SharedInfo& si = *(di.sharedInfo);

	TRACE("enter InitInterruptHandler()\n");

	DisableVBI();
	si.bInterruptAssigned = false;

	si.vertBlankSem = create_sem(0, di.name);
	if (si.vertBlankSem < 0)
		return;

	thread_id threadID = find_thread(NULL);
	thread_info threadInfo;
	status_t status = get_thread_info(threadID, &threadInfo);
	if (status == B_OK)
		status = set_sem_owner(si.vertBlankSem, threadInfo.team);

	if (status == B_OK && di.pciInfo.u.h0.interrupt_pin != 0x00
		&& di.pciInfo.u.h0.interrupt_line != 0xff) {
		status = install_io_interrupt_handler(di.pciInfo.u.h0.interrupt_line,
			InterruptHandler, (void*)(&di), 0);

		if (status == B_OK)
			si.bInterruptAssigned = true;
	}

	if (!si.bInterruptAssigned) {
		delete_sem(si.vertBlankSem);
		si.vertBlankSem = -1;
	}
}


static status_t
InitDevice(DeviceInfo& di)
{
	TRACE("enter InitDevice()\n");

	pci_info& pciInfo = di.pciInfo;
	char sharedName[B_OS_NAME_LENGTH];

	sprintf(sharedName, DEVICE_FORMAT " shared",
		pciInfo.vendor_id, pciInfo.device_id,
		pciInfo.bus, pciInfo.device, pciInfo.function);

	di.sharedArea = create_area(sharedName, (void**) &(di.sharedInfo),
		B_ANY_KERNEL_ADDRESS,
		((sizeof(SharedInfo) + (B_PAGE_SIZE - 1)) & ~(B_PAGE_SIZE - 1)),
		B_FULL_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA);
	if (di.sharedArea < 0)
		return di.sharedArea;

	SharedInfo& si = *(di.sharedInfo);

	struct frame_buffer_boot_info* bi = (struct frame_buffer_boot_info*)get_boot_item(
		FRAME_BUFFER_BOOT_INFO, NULL);

	if (bi) {
		si.has_boot_info = true;
		si.boot_width = bi->width;
		si.boot_height = bi->height;
		si.boot_depth = bi->depth;
	} else {
		si.has_boot_info = false;
	}

	si.vendorID = pciInfo.vendor_id;
	si.deviceID = pciInfo.device_id;
	si.revision = pciInfo.revision;
	si.chipType = di.pChipInfo->chipType;
	strcpy(si.chipName, di.pChipInfo->chipName);

	status_t status = MapDevice(di);
	if (status < 0) {
		delete_area(di.sharedArea);
		di.sharedArea = -1;
		di.sharedInfo = NULL;
		return status;
	}

	InitInterruptHandler(di);

	return B_OK;
}


static const ChipInfo*
GetNextSupportedDevice(uint32& pciIndex, pci_info& pciInfo)
{
	while (gPCI->get_nth_pci_info(pciIndex, &pciInfo) == B_OK) {
		if (pciInfo.vendor_id == VENDOR_ID) {
			const ChipInfo* pDevice = chipTable;

			while (pDevice->chipID != 0) {
				if (pDevice->chipID == pciInfo.device_id) {
					pciIndex++;
					return pDevice;
				}
				pDevice++;
			}
		}
		pciIndex++;
	}

	return NULL;
}


#ifdef __HAIKU__
static status_t
GetEdidFromBIOS(edid1_raw& edidRaw)
{
#define ADDRESS_SEGMENT(address) ((addr_t)(address) >> 4)
#define ADDRESS_OFFSET(address) ((addr_t)(address) & 0xf)

	bios_module_info* biosModule;
	status_t status = get_module(B_BIOS_MODULE_NAME, (module_info**)&biosModule);
	if (status != B_OK)
		return status;

	bios_state* state;
	status = biosModule->prepare(&state);
	if (status != B_OK) {
		put_module(B_BIOS_MODULE_NAME);
		return status;
	}

	bios_regs regs = {};
	regs.eax = 0x4f15;
	regs.ebx = 0;
	regs.ecx = 0;
	regs.es = 0;
	regs.edi = 0;

	status = biosModule->interrupt(state, 0x10, &regs);
	if (status == B_OK) {
		if (regs.eax != 0x4f)
			status = B_NOT_SUPPORTED;
		if ((regs.ebx & 3) == 0)
			status = B_NOT_SUPPORTED;
	}

	if (status == B_OK) {
		edid1_raw* edid = (edid1_raw*)biosModule->allocate_mem(state,
			sizeof(edid1_raw));
		if (edid == NULL) {
			status = B_NO_MEMORY;
			goto out;
		}

		regs.eax = 0x4f15;
		regs.ebx = 1;
		regs.ecx = 0;
		regs.edx = 0;
		regs.es  = ADDRESS_SEGMENT(edid);
		regs.edi = ADDRESS_OFFSET(edid);

		status = biosModule->interrupt(state, 0x10, &regs);
		if (status == B_OK) {
			if (regs.eax != 0x4f) {
				status = B_NOT_SUPPORTED;
			} else {
				uint8 sum = 0;
				uint8 allOr = 0;
				uint8* dest = (uint8*)&edidRaw;
				uint8* src = (uint8*)edid;

				for (uint32 j = 0; j < sizeof(edidRaw); j++) {
					sum += *src;
					allOr |= *src;
					*dest++ = *src++;
				}

				if (allOr == 0 || sum != 0)
					status = B_ERROR;
			}
		}
	}

out:
	biosModule->finish(state);
	put_module(B_BIOS_MODULE_NAME);
	return status;
}
#endif


status_t
init_hardware(void)
{
	if (get_module(B_PCI_MODULE_NAME, (module_info**)&gPCI) != B_OK)
		return B_ERROR;

	uint32 pciIndex = 0;
	pci_info pciInfo;
	const ChipInfo* pDevice = GetNextSupportedDevice(pciIndex, pciInfo);

	put_module(B_PCI_MODULE_NAME);

	return (pDevice == NULL ? B_ERROR : B_OK);
}


status_t
init_driver(void)
{
	if (get_module(B_PCI_MODULE_NAME, (module_info**)&gPCI) != B_OK)
		return B_ERROR;

	status_t status = gLock.Init("Trident driver lock");
	if (status < B_OK) {
		put_module(B_PCI_MODULE_NAME);
		return status;
	}

	uint32 pciIndex = 0;
	uint32 count = 0;

	while (count < MAX_DEVICES) {
		DeviceInfo& di = gDeviceInfo[count];

		const ChipInfo* pDevice = GetNextSupportedDevice(pciIndex, di.pciInfo);
		if (pDevice == NULL)
			break;

		sprintf(di.name, "graphics/" DEVICE_FORMAT,
				  di.pciInfo.vendor_id, di.pciInfo.device_id,
				  di.pciInfo.bus, di.pciInfo.device, di.pciInfo.function);

		gDeviceNames[count] = di.name;
		di.openCount = 0;
		di.sharedArea = -1;
		di.sharedInfo = NULL;
		di.pChipInfo = pDevice;
		count++;
	}

	gDeviceNames[count] = NULL;

	return B_OK;
}


void
uninit_driver(void)
{
	gLock.Delete();
	put_module(B_PCI_MODULE_NAME);
}


const char**
publish_devices(void)
{
	return (const char**)gDeviceNames;
}


device_hooks*
find_device(const char* name)
{
	int index = 0;
	while (gDeviceNames[index] != NULL) {
		if (strcmp(name, gDeviceNames[index]) == 0)
			return &gDeviceHooks;
		index++;
	}

	return NULL;
}


static status_t
device_open(const char* name, uint32 /*flags*/, void** cookie)
{
	status_t status = B_OK;

	int32 index = 0;
	while (gDeviceNames[index] != NULL && (strcmp(name, gDeviceNames[index]) != 0))
		index++;

	if (gDeviceNames[index] == NULL)
		return B_BAD_VALUE;

	DeviceInfo& di = gDeviceInfo[index];

	gLock.Acquire();

	if (di.openCount == 0)
		status = InitDevice(di);

	gLock.Release();

	if (status == B_OK) {
		di.openCount++;
		*cookie = &di;
	}

	return status;
}


static status_t
device_read(void* dev, off_t pos, void* buf, size_t* len)
{
	(void)dev; (void)pos; (void)buf;
	*len = 0;
	return B_NOT_ALLOWED;
}


static status_t
device_write(void* dev, off_t pos, const void* buf, size_t* len)
{
	(void)dev; (void)pos; (void)buf;
	*len = 0;
	return B_NOT_ALLOWED;
}


static status_t
device_close(void* dev)
{
	(void)dev;
	return B_NO_ERROR;
}


static status_t
device_free(void* dev)
{
	DeviceInfo& di = *((DeviceInfo*)dev);
	SharedInfo& si = *(di.sharedInfo);
	pci_info& pciInfo = di.pciInfo;

	gLock.Acquire();

	if (di.openCount <= 1) {
		DisableVBI();

		if (si.bInterruptAssigned) {
			remove_io_interrupt_handler(pciInfo.u.h0.interrupt_line, InterruptHandler, &di);
		}

		if (si.vertBlankSem >= 0)
			delete_sem(si.vertBlankSem);
		si.vertBlankSem = -1;

		UnmapDevice(di);

		delete_area(di.sharedArea);
		di.sharedArea = -1;
		di.sharedInfo = NULL;
	}

	if (di.openCount > 0)
		di.openCount--;

	gLock.Release();

	return B_OK;
}


static status_t
device_ioctl(void* dev, uint32 msg, void* buf, size_t len)
{
	DeviceInfo& di = *((DeviceInfo*)dev);
	(void)len;

	switch (msg) {
		case B_GET_ACCELERANT_SIGNATURE:
			if (user_strlcpy((char*)buf, "trident.accelerant", B_FILE_NAME_LENGTH) < 0)
				return B_BAD_ADDRESS;
			return B_OK;

		case TRIDENT_DEVICE_NAME:
			if (user_strlcpy((char*)buf, di.name, B_OS_NAME_LENGTH) < 0)
				return B_BAD_ADDRESS;
			((char*)buf)[B_OS_NAME_LENGTH -1] = '\0';
			return B_OK;

		case TRIDENT_GET_PRIVATE_DATA:
		{
			TridentGetPrivateData gpd;
			if (user_memcpy(&gpd, buf, sizeof(TridentGetPrivateData)) != B_OK)
				return B_BAD_ADDRESS;

			if (gpd.magic == TRIDENT_PRIVATE_DATA_MAGIC) {
				gpd.sharedInfoArea = di.sharedArea;
				if (user_memcpy(buf, &gpd, sizeof(TridentGetPrivateData)) != B_OK)
					return B_BAD_ADDRESS;
				return B_OK;
			}
			break;
		}

		case TRIDENT_GET_EDID:
		{
#ifdef __HAIKU__
			edid1_raw rawEdid;
			status_t status = GetEdidFromBIOS(rawEdid);
			if (status == B_OK) {
				if (user_memcpy(buf, &rawEdid, sizeof(rawEdid)) != B_OK)
					return B_BAD_ADDRESS;
			}
			return status;
#else
			return B_UNSUPPORTED;
#endif
		}

		case TRIDENT_RUN_INTERRUPTS:
		{
			bool bEnable;
			if (user_memcpy(&bEnable, buf, sizeof(bool)) != B_OK)
				return B_BAD_ADDRESS;

			if (bEnable)
				EnableVBI();
			else
				DisableVBI();
			return B_OK;
		}

		case TRIDENT_ENABLE_MMIO:
		{
			EnableMMIO(di);
			return B_OK;
		}
	}

	return B_DEV_INVALID_IOCTL;
}
